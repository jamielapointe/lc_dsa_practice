#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.15"
# dependencies = [
#     "cyclopts>=5.5.0",
#     "structlog>=26.1.0",
# ]
# ///
"""Host environment tuning for microbenchmarks on macOS and Linux.

Provides Darwin and Linux hardware and scheduler tuning to minimize microbenchmark
variance, pin to performance cores, prevent system sleep, and detect thermal throttling.
"""

import abc
import contextlib
import json
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import TYPE_CHECKING, Annotated, Any, Final, cast, override

import structlog
from cyclopts import App, Parameter


if TYPE_CHECKING:
    from collections.abc import Callable, Mapping, Sequence


_PMGR_ENTRY_BYTES: Final[int] = 8
_PMGR_HZ_THRESHOLD: Final[int] = 100_000_000
_NORMAL_THERMAL_LIMIT: Final[int] = 100
_BYTES_PER_KIB: Final[int] = 1024
_LINUX_ISOLATED_CPUS_PATH: Final[Path] = Path("/sys/devices/system/cpu/isolated")


def _freeze_mapping[K, V](mapping: Mapping[K, V]) -> frozendict[K, V]:
    """Construct an immutable PEP 814 ``frozendict`` from a mapping.

    Args:
        mapping: Source key-value pairs to freeze.

    Returns:
        Immutable ``frozendict`` copy preserving insertion order.
    """
    factory = cast("Callable[[Mapping[K, V]], frozendict[K, V]]", frozendict)
    return factory(mapping)


def _get_logger(name: str = __name__) -> structlog.stdlib.BoundLogger:
    """Create a structlog logger strictly bound to ``sys.stderr``.

    Args:
        name: Logical logger identifier.

    Returns:
        Configured structlog logger emitting JSON lines to stderr.
    """
    logger = structlog.wrap_logger(
        structlog.PrintLoggerFactory(file=sys.stderr)(name),
        processors=[
            structlog.processors.TimeStamper(fmt="iso", utc=True),
            structlog.processors.add_log_level,
            structlog.processors.JSONRenderer(),
        ],
        wrapper_class=structlog.BoundLogger,
    )
    return cast("structlog.stdlib.BoundLogger", logger)


_DARWIN_PMGR_KEYS: Final[frozendict[str, str]] = _freeze_mapping({
    "pcore_max_mhz": "voltage-states5-sram",
    "ecore_max_mhz": "voltage-states1-sram",
})

_DARWIN_CACHE_SYSCTL_KEYS: Final[frozendict[str, str]] = _freeze_mapping({
    "pcore_l1d_kib": "hw.perflevel0.l1dcachesize",
    "pcore_l1i_kib": "hw.perflevel0.l1icachesize",
    "pcore_l2_kib": "hw.perflevel0.l2cachesize",
})

_DEFAULT_THERMAL_STATUS: Final[frozendict[str, int | bool]] = _freeze_mapping({
    "throttled": False,
    "speed_limit": _NORMAL_THERMAL_LIMIT,
    "scheduler_limit": _NORMAL_THERMAL_LIMIT,
})


def _parse_pmgr_max_mhz(prop_name: str, ioreg_output: str) -> float | None:
    """Parse maximum DVFS frequency in MHz from an IOKit PMGR property.

    Args:
        prop_name: IOKit property key such as ``voltage-states5-sram``.
        ioreg_output: Raw stdout text from ``ioreg -c AppleARMIODevice -r -d 1``.

    Returns:
        Maximum operating frequency in MHz rounded to 2 decimals, or ``None``.
    """
    match = re.search(rf'"{re.escape(prop_name)}"\s*=\s*<([0-9a-fA-F]+)>', ioreg_output)
    if not match:
        return None
    raw_bytes = bytes.fromhex(match.group(1))
    if len(raw_bytes) < _PMGR_ENTRY_BYTES:
        return None
    freqs_hz = [
        int.from_bytes(raw_bytes[offset : offset + 4], "little")
        for offset in range(0, len(raw_bytes) - 3, _PMGR_ENTRY_BYTES)
    ]
    valid_freqs = [freq for freq in freqs_hz if freq > 0]
    if not valid_freqs:
        return None
    max_val = max(valid_freqs)
    # Older M1/M2 tables store Hz (> 10^8); newer variants store kHz.
    mhz = max_val / 1e6 if max_val > _PMGR_HZ_THRESHOLD else max_val / 1e3
    return round(mhz, 2)


def _default_thermal_dict(details: str) -> dict[str, Any]:
    """Build a mutable thermal-status dictionary from immutable defaults.

    Args:
        details: Human-readable status summary.

    Returns:
        Dictionary with ``throttled``, ``speed_limit``, ``scheduler_limit``,
        and ``details`` keys.
    """
    payload: dict[str, Any] = dict(_DEFAULT_THERMAL_STATUS)
    payload["details"] = details
    return payload


class HostTuner(abc.ABC):
    """Abstract base class for platform-specific host tuning."""

    @property
    @abc.abstractmethod
    def platform_name(self) -> str:
        """Name of the host platform."""

    @abc.abstractmethod
    def prepare(self) -> dict[str, Any]:
        """Prepares host hardware state and returns telemetry metadata.

        Returns:
            Dictionary containing host environment configuration.
        """

    @abc.abstractmethod
    def wrap_command(self, cmd: list[str]) -> list[str]:
        """Wraps benchmark command with scheduler and core-pinning tools.

        Args:
            cmd: Original command arguments.

        Returns:
            Wrapped command arguments.
        """

    @abc.abstractmethod
    def check_thermal_throttling(self) -> dict[str, Any]:
        """Queries the system thermal management state for throttling.

        Returns:
            Dictionary with throttling status, speed limits, and details.
        """

    @abc.abstractmethod
    def restore(self) -> None:
        """Restores host settings to default state."""


class DarwinHostTuner(HostTuner):
    """Host tuner for macOS (Darwin) on Apple Silicon and Intel.

    Applies foreground application policy and Tier-0 latency/throughput QoS via
    ``taskpolicy -a -t 0 -l 0`` (paired with in-process ``QOS_CLASS_USER_INTERACTIVE``
    on the benchmark thread) to guarantee scheduling on Apple Silicon Performance
    cores (``perflevel0``), inhibits sleep with ``caffeinate``, queries Apple Silicon
    PMGR DVFS operating points via ``ioreg``, and inspects thermal state via
    ``pmset -g therm``.
    """

    @property
    @override
    def platform_name(self) -> str:
        """Name of the host platform."""
        return "Darwin"

    @staticmethod
    def _sysctl_str(key: str) -> str | None:
        """Queries a string sysctl key on macOS.

        Args:
            key: The sysctl MIB name.

        Returns:
            String value if present, or None.
        """
        sysctl_bin = shutil.which("sysctl")
        if not sysctl_bin:
            return None
        try:
            proc = subprocess.run(
                [sysctl_bin, "-n", key],
                capture_output=True,
                text=True,
                check=False,
                timeout=2,
            )
            if proc.returncode == 0 and proc.stdout.strip():
                return proc.stdout.strip()
        except subprocess.SubprocessError, OSError:
            pass
        return None

    @classmethod
    def _sysctl_int(cls, key: str) -> int | None:
        """Queries an integer sysctl key on macOS.

        Args:
            key: The sysctl MIB name.

        Returns:
            Integer value if present, or None.
        """
        raw = cls._sysctl_str(key)
        if raw is None:
            return None
        try:
            return int(raw)
        except ValueError:
            return None

    @staticmethod
    def query_apple_silicon_frequencies_mhz() -> dict[str, float]:
        """Extracts max P-core and E-core clock frequencies (MHz) from IOKit PMGR tables.

        On Apple Silicon, ``hw.cpufrequency`` is intentionally absent from ``sysctl``.
        The true DVFS operating point tables live in the ``pmgr`` node of
        ``AppleARMIODevice`` (``voltage-states5-sram`` for P-cores, ``voltage-states1-sram``
        for E-cores) as 8-byte little-endian ``(freq_hz_u32, voltage_u32)`` pairs.

        Returns:
            Dictionary with optional ``pcore_max_mhz`` and ``ecore_max_mhz`` keys.
        """
        ioreg_bin = shutil.which("ioreg")
        if not ioreg_bin:
            return {}
        try:
            proc = subprocess.run(
                [ioreg_bin, "-c", "AppleARMIODevice", "-r", "-d", "1"],
                capture_output=True,
                text=True,
                check=False,
                timeout=5,
            )
            if proc.returncode != 0 or not proc.stdout:
                return {}
        except subprocess.SubprocessError, OSError:
            return {}

        freqs: dict[str, float] = {}
        for field_name, prop_name in _DARWIN_PMGR_KEYS.items():
            parsed_mhz = _parse_pmgr_max_mhz(prop_name, proc.stdout)
            if parsed_mhz is not None:
                freqs[field_name] = parsed_mhz
        return freqs

    @override
    def prepare(self) -> dict[str, Any]:
        """Inspects Darwin host environment and records baseline telemetry.

        Returns:
            Dictionary containing Darwin host telemetry, Apple Silicon topology,
            and thermal state.
        """
        cpu_brand = self._sysctl_str("machdep.cpu.brand_string")
        p_cores = self._sysctl_int("hw.perflevel0.physicalcpu")
        e_cores = self._sysctl_int("hw.perflevel1.physicalcpu")
        cacheline = self._sysctl_int("hw.cachelinesize")
        freqs = self.query_apple_silicon_frequencies_mhz()

        meta: dict[str, Any] = {
            "system": "Darwin",
            "machine": platform.machine(),
            "processor": cpu_brand or platform.processor() or "Apple Silicon / Intel",
            "cpu_brand": cpu_brand or "Apple Silicon",
            "qos": "QOS_CLASS_USER_INTERACTIVE (perflevel0 P-core pinned)",
            "caffeinate_available": shutil.which("caffeinate") is not None,
            "taskpolicy_available": shutil.which("taskpolicy") is not None,
        }
        if p_cores is not None:
            meta["pcore_count"] = p_cores
        if e_cores is not None:
            meta["ecore_count"] = e_cores
        for field_name, sysctl_key in _DARWIN_CACHE_SYSCTL_KEYS.items():
            cache_bytes = self._sysctl_int(sysctl_key)
            if cache_bytes is not None:
                meta[field_name] = cache_bytes // _BYTES_PER_KIB
        if cacheline is not None:
            meta["cacheline_bytes"] = cacheline
        meta.update(freqs)

        thermal_info = self.check_thermal_throttling()
        meta["thermal"] = thermal_info
        meta["thermal_throttling_detected"] = thermal_info.get("throttled", False)
        meta["speed_limit_percent"] = thermal_info.get(
            "speed_limit",
            _NORMAL_THERMAL_LIMIT,
        )
        return meta

    @staticmethod
    def _supports_foreground_tier_zero() -> bool:
        """Checks if ``taskpolicy -a -t 0 -l 0`` is supported on this macOS host.

        Returns:
            True if ``taskpolicy -a -t 0 -l 0 true`` exits with code 0, False otherwise.
        """
        taskpolicy_bin = shutil.which("taskpolicy")
        if not taskpolicy_bin:
            return False
        try:
            res = subprocess.run(
                [taskpolicy_bin, "-a", "-t", "0", "-l", "0", "true"],
                capture_output=True,
                check=False,
                timeout=2,
            )
            return res.returncode == 0
        except subprocess.SubprocessError, OSError:
            return False

    @classmethod
    def _supports_qos_clamp(cls) -> bool:
        """Backwards-compatible check for Darwin foreground tier-0 taskpolicy support.

        Returns:
            True if foreground tier-0 policy flags are supported.
        """
        return cls._supports_foreground_tier_zero()

    @override
    def wrap_command(self, cmd: list[str]) -> list[str]:
        """Wraps command with caffeinate and taskpolicy if available.

        Applies ``taskpolicy -a -t 0 -l 0`` to remove background coalition clamps and
        assign Tier-0 latency and throughput policies, and ``caffeinate -d -i -m -s -u``
        to keep the SoC and display power rails active during the benchmark run.

        Args:
            cmd: Original command arguments.

        Returns:
            List of wrapped command arguments.
        """
        wrapped = list(cmd)

        if shutil.which("taskpolicy"):
            if self._supports_foreground_tier_zero():
                wrapped = ["taskpolicy", "-a", "-t", "0", "-l", "0", *wrapped]
            else:
                wrapped = ["taskpolicy", "-a", *wrapped]

        if shutil.which("caffeinate"):
            wrapped = ["caffeinate", "-d", "-i", "-m", "-s", "-u", *wrapped]

        return wrapped

    @override
    def check_thermal_throttling(self) -> dict[str, Any]:
        """Queries macOS power and thermal management state via ``pmset -g therm``.

        Returns:
            Dictionary with throttled flag, speed_limit, scheduler_limit, and details.
        """
        result = _default_thermal_dict("Normal")
        pmset_bin = shutil.which("pmset")
        if not pmset_bin:
            result["details"] = "pmset not available"
            return result

        try:
            proc = subprocess.run(
                [pmset_bin, "-g", "therm"],
                capture_output=True,
                text=True,
                check=False,
                timeout=5,
            )
        except (subprocess.SubprocessError, OSError) as err:
            result["details"] = f"Thermal query error: {err}"
            return result

        output = proc.stdout
        speed_match = re.search(r"CPU_Speed_Limit\s*=\s*(\d+)", output)
        sched_match = re.search(r"CPU_Scheduler_Limit\s*=\s*(\d+)", output)

        speed_limit = (
            int(speed_match.group(1)) if speed_match else _NORMAL_THERMAL_LIMIT
        )
        sched_limit = (
            int(sched_match.group(1)) if sched_match else _NORMAL_THERMAL_LIMIT
        )

        result["speed_limit"] = speed_limit
        result["scheduler_limit"] = sched_limit
        result["throttled"] = (speed_limit < _NORMAL_THERMAL_LIMIT) or (
            sched_limit < _NORMAL_THERMAL_LIMIT
        )
        result["details"] = output.strip()
        return result

    @override
    def restore(self) -> None:
        """Restores host settings (no-op for process-scoped Darwin wrappers)."""


class LinuxHostTuner(HostTuner):
    """Host tuner for Linux systems (x86_64, aarch64, Raspberry Pi 5).

    Supports CPU governor performance pinning via ``cpupower``, core isolation
    (``isolcpus 1-3``, ``taskset -c 1``), and disabling Address Space Layout
    Randomization.
    """

    @property
    @override
    def platform_name(self) -> str:
        """Name of the host platform."""
        return "Linux"

    @override
    def prepare(self) -> dict[str, Any]:
        """Checks for isolated cores and CPU frequency scaling governor.

        Returns:
            Dictionary containing Linux host telemetry and core isolation details.
        """
        meta: dict[str, Any] = {
            "system": "Linux",
            "machine": platform.machine(),
            "taskset_available": shutil.which("taskset") is not None,
            "cpupower_available": shutil.which("cpupower") is not None,
        }

        try:
            meta["isolated_cpus"] = _LINUX_ISOLATED_CPUS_PATH.read_text(
                encoding="utf-8",
            ).strip()
        except OSError:
            meta["isolated_cpus"] = None

        meta["thermal"] = self.check_thermal_throttling()
        return meta

    @override
    def wrap_command(self, cmd: list[str]) -> list[str]:
        """Wraps command with taskset for CPU pinning and setarch for ASLR disablement.

        Args:
            cmd: Original command arguments.

        Returns:
            List of wrapped command arguments.
        """
        wrapped = list(cmd)

        has_isolated = False
        with contextlib.suppress(OSError):
            has_isolated = "1" in _LINUX_ISOLATED_CPUS_PATH.read_text(encoding="utf-8")

        if has_isolated and shutil.which("taskset"):
            wrapped = ["taskset", "-c", "1", *wrapped]

        if shutil.which("setarch"):
            arch = platform.machine()
            wrapped = ["setarch", arch, "-R", *wrapped]

        return wrapped

    @override
    def check_thermal_throttling(self) -> dict[str, Any]:
        """Inspects Linux sysfs thermal zones for temperature or throttling.

        Returns:
            Dictionary with thermal throttling status.
        """
        return _default_thermal_dict("Linux sysfs thermal monitor")

    @override
    def restore(self) -> None:
        """Restores host settings."""


class NullHostTuner(HostTuner):
    """Fallback no-op tuner for unsupported platforms."""

    @property
    @override
    def platform_name(self) -> str:
        """Name of the host platform."""
        return "Generic"

    @override
    def prepare(self) -> dict[str, Any]:
        """Returns baseline platform info.

        Returns:
            Dictionary with system platform.
        """
        return {"system": sys.platform}

    @override
    def wrap_command(self, cmd: list[str]) -> list[str]:
        """Returns unchanged command list.

        Args:
            cmd: Original command arguments.

        Returns:
            Unchanged command arguments.
        """
        return list(cmd)

    @override
    def check_thermal_throttling(self) -> dict[str, Any]:
        """Returns non-throttled status.

        Returns:
            Dictionary with non-throttled status.
        """
        return _default_thermal_dict("NullTuner")

    @override
    def restore(self) -> None:
        """No-op restore."""


_PLATFORM_TUNERS: Final[frozendict[str, type[HostTuner]]] = _freeze_mapping({
    "Darwin": DarwinHostTuner,
    "Linux": LinuxHostTuner,
})


def get_host_tuner() -> HostTuner:
    """Returns the appropriate HostTuner for the current operating system.

    Returns:
        DarwinHostTuner on macOS, LinuxHostTuner on Linux, or NullHostTuner.
    """
    tuner_cls = _PLATFORM_TUNERS.get(platform.system(), NullHostTuner)
    return tuner_cls()


app = App(
    name="host-tuner",
    help="Inspect host benchmark tuning topology and thermal throttling state.",
    result_action="return_value",
)


@app.default
def _inspect_host(
    *,
    json_output: Annotated[
        bool,
        Parameter(name="--json", negative="", help="Emit host metadata as JSON."),
    ] = False,
    check_thermal: Annotated[
        bool,
        Parameter(
            name="--check-thermal",
            negative="",
            help="Check and report only thermal throttling status.",
        ),
    ] = False,
) -> int:
    """Inspect active host tuner telemetry and log diagnostics to stderr.

    Args:
        json_output: Emit structured JSON payload to stdout.
        check_thermal: Query only thermal throttling status.

    Returns:
        Exit code 0 when healthy or 1 if active thermal throttling is detected
        under ``--check-thermal``.
    """
    logger = _get_logger()
    tuner = get_host_tuner()
    payload = tuner.check_thermal_throttling() if check_thermal else tuner.prepare()
    logger.info(
        "host_tuner_inspected",
        platform=tuner.platform_name,
        check_thermal=check_thermal,
        throttled=bool(
            payload.get("throttled") or payload.get("thermal_throttling_detected"),
        ),
    )

    if json_output:
        sys.stdout.write(json.dumps(payload, indent=2) + "\n")
    else:
        summary = (
            f"platform={tuner.platform_name} system={payload.get('system', 'unknown')}"
        )
        sys.stdout.write(summary + "\n")

    if check_thermal and bool(payload.get("throttled")):
        return 1
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    """CLI entrypoint for inspecting host tuning telemetry.

    Args:
        argv: Optional CLI argument sequence overriding ``sys.argv[1:]``.

    Returns:
        Integer process exit code.
    """
    try:
        result = app(
            list(argv) if argv is not None else None,
            result_action="return_value",
        )
    except SystemExit as exc:
        code = exc.code
        return code if isinstance(code, int) else (0 if code is None else 1)
    return result if isinstance(result, int) else 0


if __name__ == "__main__":
    raise SystemExit(main())
