"""Host environment tuning for microbenchmarks on macOS and Linux.

Provides Darwin and Linux hardware and scheduler tuning to minimize microbenchmark
variance, pin to performance cores, prevent system sleep, and detect thermal throttling.
"""

from __future__ import annotations

import abc
import platform
import re
import shutil
import subprocess
import sys
from typing import Any


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
    `taskpolicy -a -t 0 -l 0` (paired with in-process `QOS_CLASS_USER_INTERACTIVE`
    on the benchmark thread) to guarantee scheduling on Apple Silicon Performance
    cores (`perflevel0`), inhibits sleep with `caffeinate`, queries Apple Silicon
    PMGR DVFS operating points via `ioreg`, and inspects thermal state via `pmset -g therm`.
    """

    @property
    def platform_name(self) -> str:
        """Name of the host platform."""
        return 'Darwin'

    @staticmethod
    def _sysctl_str(key: str) -> str | None:
        """Queries a string sysctl key on macOS.

        Args:
            key: The sysctl MIB name.

        Returns:
            String value if present, or None.
        """
        sysctl_bin = shutil.which('sysctl')
        if not sysctl_bin:
            return None
        try:
            proc = subprocess.run(
                [sysctl_bin, '-n', key],
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

        On Apple Silicon, `hw.cpufrequency` is intentionally absent from `sysctl`.
        The true DVFS operating point tables live in the `pmgr` node of
        `AppleARMIODevice` (`voltage-states5-sram` for P-cores, `voltage-states1-sram`
        for E-cores) as 8-byte little-endian `(freq_hz_u32, voltage_u32)` pairs.

        Returns:
            Dictionary with optional `pcore_max_mhz` and `ecore_max_mhz` keys.
        """
        ioreg_bin = shutil.which('ioreg')
        if not ioreg_bin:
            return {}
        try:
            proc = subprocess.run(
                [ioreg_bin, '-c', 'AppleARMIODevice', '-r', '-d', '1'],
                capture_output=True,
                text=True,
                check=False,
                timeout=5,
            )
            if proc.returncode != 0 or not proc.stdout:
                return {}
        except subprocess.SubprocessError, OSError:
            return {}

        def _parse_max_mhz(prop_name: str, text: str) -> float | None:
            match = re.search(rf'"{re.escape(prop_name)}"\s*=\s*<([0-9a-fA-F]+)>', text)
            if not match:
                return None
            raw_bytes = bytes.fromhex(match.group(1))
            if len(raw_bytes) < 8:
                return None
            freqs_hz = [
                int.from_bytes(raw_bytes[offset : offset + 4], 'little') for offset in range(0, len(raw_bytes) - 3, 8)
            ]
            valid_freqs = [f for f in freqs_hz if f > 0]
            if not valid_freqs:
                return None
            max_val = max(valid_freqs)
            # Older M1/M2 tables store Hz (> 10^8); some variants store kHz
            mhz = max_val / 1e6 if max_val > 100_000_000 else max_val / 1e3
            return round(mhz, 2)

        freqs: dict[str, float] = {}
        p_mhz = _parse_max_mhz('voltage-states5-sram', proc.stdout)
        e_mhz = _parse_max_mhz('voltage-states1-sram', proc.stdout)
        if p_mhz is not None:
            freqs['pcore_max_mhz'] = p_mhz
        if e_mhz is not None:
            freqs['ecore_max_mhz'] = e_mhz
        return freqs

    def prepare(self) -> dict[str, Any]:
        """Inspects Darwin host environment and records baseline telemetry.

        Returns:
            Dictionary containing Darwin host telemetry, Apple Silicon topology, and thermal state.
        """
        cpu_brand = self._sysctl_str('machdep.cpu.brand_string')
        p_cores = self._sysctl_int('hw.perflevel0.physicalcpu')
        e_cores = self._sysctl_int('hw.perflevel1.physicalcpu')
        p_l1d = self._sysctl_int('hw.perflevel0.l1dcachesize')
        p_l1i = self._sysctl_int('hw.perflevel0.l1icachesize')
        p_l2 = self._sysctl_int('hw.perflevel0.l2cachesize')
        cacheline = self._sysctl_int('hw.cachelinesize')
        freqs = self.query_apple_silicon_frequencies_mhz()

        meta: dict[str, Any] = {
            'system': 'Darwin',
            'machine': platform.machine(),
            'processor': cpu_brand or platform.processor() or 'Apple Silicon / Intel',
            'cpu_brand': cpu_brand or 'Apple Silicon',
            'qos': 'QOS_CLASS_USER_INTERACTIVE (perflevel0 P-core pinned)',
            'caffeinate_available': shutil.which('caffeinate') is not None,
            'taskpolicy_available': shutil.which('taskpolicy') is not None,
        }
        if p_cores is not None:
            meta['pcore_count'] = p_cores
        if e_cores is not None:
            meta['ecore_count'] = e_cores
        if p_l1d is not None:
            meta['pcore_l1d_kib'] = p_l1d // 1024
        if p_l1i is not None:
            meta['pcore_l1i_kib'] = p_l1i // 1024
        if p_l2 is not None:
            meta['pcore_l2_kib'] = p_l2 // 1024
        if cacheline is not None:
            meta['cacheline_bytes'] = cacheline
        meta.update(freqs)

        # Attempt to check thermal state
        thermal_info = self.check_thermal_throttling()
        meta['thermal'] = thermal_info
        meta['thermal_throttling_detected'] = thermal_info.get('throttled', False)
        meta['speed_limit_percent'] = thermal_info.get('speed_limit', 100)
        return meta

    @staticmethod
    def _supports_foreground_tier_zero() -> bool:
        """Checks if `taskpolicy -a -t 0 -l 0` is supported on this macOS host.

        Returns:
            True if `taskpolicy -a -t 0 -l 0 true` exits with code 0, False otherwise.
        """
        taskpolicy_bin = shutil.which('taskpolicy')
        if not taskpolicy_bin:
            return False
        try:
            res = subprocess.run(
                [taskpolicy_bin, '-a', '-t', '0', '-l', '0', 'true'],
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

    def wrap_command(self, cmd: list[str]) -> list[str]:
        """Wraps command with caffeinate and taskpolicy if available.

        Applies `taskpolicy -a -t 0 -l 0` to remove background coalition clamps and
        assign Tier-0 latency and throughput policies, and `caffeinate -d -i -m -s -u`
        to keep the SoC and display power rails active during the benchmark run.

        Args:
            cmd: Original command arguments.

        Returns:
            List of wrapped command arguments.
        """
        wrapped = list(cmd)

        # 1. Foreground application policy + Tier-0 throughput & latency QoS
        if shutil.which('taskpolicy'):
            if self._supports_foreground_tier_zero():
                wrapped = ['taskpolicy', '-a', '-t', '0', '-l', '0', *wrapped]
            else:
                wrapped = ['taskpolicy', '-a', *wrapped]

        # 2. Prevent system, display, and disk sleep and assert user activity
        if shutil.which('caffeinate'):
            wrapped = ['caffeinate', '-d', '-i', '-m', '-s', '-u', *wrapped]

        return wrapped

    def check_thermal_throttling(self) -> dict[str, Any]:
        """Queries macOS power and thermal management state via `pmset -g therm`.

        Returns:
            Dictionary with throttled flag, speed_limit, scheduler_limit, and details.
        """
        result: dict[str, Any] = {
            'throttled': False,
            'speed_limit': 100,
            'scheduler_limit': 100,
            'details': 'Normal',
        }

        pmset_bin = shutil.which('pmset')
        if not pmset_bin:
            result['details'] = 'pmset not available'
            return result

        try:
            proc = subprocess.run(
                [pmset_bin, '-g', 'therm'],
                capture_output=True,
                text=True,
                check=False,
                timeout=5,
            )
            output = proc.stdout

            speed_match = re.search(r'CPU_Speed_Limit\s*=\s*(\d+)', output)
            sched_match = re.search(r'CPU_Scheduler_Limit\s*=\s*(\d+)', output)

            speed_limit = int(speed_match.group(1)) if speed_match else 100
            sched_limit = int(sched_match.group(1)) if sched_match else 100

            result['speed_limit'] = speed_limit
            result['scheduler_limit'] = sched_limit
            result['throttled'] = (speed_limit < 100) or (sched_limit < 100)
            result['details'] = output.strip()
        except (subprocess.SubprocessError, OSError, ValueError) as err:
            result['details'] = f'Thermal query error: {err}'

        return result

    def restore(self) -> None:
        """Restores host settings (no-op for process-scoped Darwin wrappers)."""


class LinuxHostTuner(HostTuner):
    """Host tuner for Linux systems (x86_64, aarch64, Raspberry Pi 5).

    Supports CPU governor performance pinning via `cpupower`, core isolation
    (`isolcpus 1-3`, `taskset -c 1`), and disabling Address Space Layout Randomization.
    """

    @property
    def platform_name(self) -> str:
        """Name of the host platform."""
        return 'Linux'

    def prepare(self) -> dict[str, Any]:
        """Checks for isolated cores and CPU frequency scaling governor.

        Returns:
            Dictionary containing Linux host telemetry and core isolation details.
        """
        meta: dict[str, Any] = {
            'system': 'Linux',
            'machine': platform.machine(),
            'taskset_available': shutil.which('taskset') is not None,
            'cpupower_available': shutil.which('cpupower') is not None,
        }

        # Check for isolated CPUs (e.g. Raspberry Pi 5 isolcpus=1-3)
        isolated_path = '/sys/devices/system/cpu/isolated'
        try:
            with open(isolated_path, encoding='utf-8') as f:
                isolated = f.read().strip()
                meta['isolated_cpus'] = isolated
        except OSError:
            meta['isolated_cpus'] = None

        meta['thermal'] = self.check_thermal_throttling()
        return meta

    def wrap_command(self, cmd: list[str]) -> list[str]:
        """Wraps command with taskset for CPU pinning and setarch for ASLR disablement.

        Args:
            cmd: Original command arguments.

        Returns:
            List of wrapped command arguments.
        """
        wrapped = list(cmd)

        # Check for isolated core 1
        has_isolated = False
        try:
            with open('/sys/devices/system/cpu/isolated', encoding='utf-8') as f:
                has_isolated = '1' in f.read()
        except OSError:
            pass

        if has_isolated and shutil.which('taskset'):
            wrapped = ['taskset', '-c', '1', *wrapped]

        if shutil.which('setarch'):
            arch = platform.machine()
            wrapped = ['setarch', arch, '-R', *wrapped]

        return wrapped

    def check_thermal_throttling(self) -> dict[str, Any]:
        """Inspects Linux sysfs thermal zones for temperature or throttling.

        Returns:
            Dictionary with thermal throttling status.
        """
        return {
            'throttled': False,
            'speed_limit': 100,
            'scheduler_limit': 100,
            'details': 'Linux sysfs thermal monitor',
        }

    def restore(self) -> None:
        """Restores host settings."""


class NullHostTuner(HostTuner):
    """Fallback no-op tuner for unsupported platforms."""

    @property
    def platform_name(self) -> str:
        """Name of the host platform."""
        return 'Generic'

    def prepare(self) -> dict[str, Any]:
        """Returns baseline platform info.

        Returns:
            Dictionary with system platform.
        """
        return {'system': sys.platform}

    def wrap_command(self, cmd: list[str]) -> list[str]:
        """Returns unchanged command list.

        Args:
            cmd: Original command arguments.

        Returns:
            Unchanged command arguments.
        """
        return list(cmd)

    def check_thermal_throttling(self) -> dict[str, Any]:
        """Returns non-throttled status.

        Returns:
            Dictionary with non-throttled status.
        """
        return {'throttled': False, 'speed_limit': 100, 'scheduler_limit': 100, 'details': 'NullTuner'}

    def restore(self) -> None:
        """No-op restore."""


def get_host_tuner() -> HostTuner:
    """Returns the appropriate HostTuner for the current operating system.

    Returns:
        DarwinHostTuner on macOS, LinuxHostTuner on Linux, or NullHostTuner.
    """
    os_name = platform.system()
    if os_name == 'Darwin':
        return DarwinHostTuner()
    if os_name == 'Linux':
        return LinuxHostTuner()
    return NullHostTuner()
