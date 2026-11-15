# /// script
# requires-python = ">=3.15"
# dependencies = [
#     "cyclopts>=5.5.0",
#     "structlog>=26.1.0",
# ]
# ///
"""Orchestrate and verify the multi-process SPMC shared-memory camera example.

Spawns ``shm_pubsub_consumer`` and ``shm_pubsub_producer`` as separate OS
processes, ensures the consumer is waiting before the producer starts, captures
both processes' structured logs, and verifies:

* Initialization handshake ordering (`init_waiting_for_channel`,
  `channel_created`, `ready_and_waiting mode=kLatest`,
  `consumer_ready_starting_loop`).
* Producer published all expected camera frames (`1..N`).
* Consumer received frames in strictly increasing sequence order.
* Every received frame's computed IEEE 802.3 CRC-32 matches both its frame
  header CRC-32 and the producer's logged CRC-32 for that sequence number.
* ``ShmReadMode::kLatest`` missed-frame accounting matches exact sequence gaps
  and demonstrates the consumer falling 1-2 frames behind the producer.
"""

import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING, Annotated, Final, cast

import cyclopts
import structlog
from cyclopts import Parameter, Token, validators


if TYPE_CHECKING:
    from collections.abc import Sequence


MAX_EXPECTED_SKIP_FRAMES: Final[int] = 2
DEFAULT_SEED: Final[int] = 0xC0FFEE01


def _get_stderr_logger() -> structlog.BoundLogger:
    """Builds an isolated structlog logger bound to the active ``sys.stderr`` stream.

    Returns:
        Bound structlog logger writing to ``sys.stderr``.
    """
    logger = structlog.wrap_logger(
        structlog.PrintLogger(file=sys.stderr),
        processors=[
            structlog.stdlib.add_log_level,
            structlog.processors.TimeStamper(fmt="iso", utc=True),
            structlog.dev.ConsoleRenderer(colors=False),
        ],
        wrapper_class=structlog.BoundLogger,
    )
    return cast("structlog.BoundLogger", logger)


_PRODUCER_FRAME_RE = re.compile(
    r"^\[PRODUCER\]\s+event=frame_published\s+frame=(\d+)\s+seq=(\d+)\s+size=(\d+)\s+crc32=(0x[0-9a-fA-F]{8})\s*$"
)
_PRODUCER_SHUTDOWN_RE = re.compile(
    r"^\[PRODUCER\]\s+event=shutdown\s+published_count=(\d+)\s*$"
)
_PRODUCER_EVENT_RE = re.compile(r"^\[PRODUCER\]\s+event=([a-zA-Z0-9_]+)")

_CONSUMER_READY_RE = re.compile(
    r"^\[CONSUMER\]\s+event=ready_and_waiting\s+mode=(\S+)\s+slots=(\d+)\s+slot_size=(\d+)"
)
_CONSUMER_FRAME_RE = re.compile(
    r"^\[CONSUMER\]\s+event=frame_received\s+seq=(\d+)\s+missed=(\d+)\s+size=(\d+)\s+"
    r"header_crc32=(0x[0-9a-fA-F]{8})\s+computed_crc32=(0x[0-9a-fA-F]{8})\s+crc_ok=(true|false)\s*$"
)
_CONSUMER_SHUTDOWN_RE = re.compile(
    r"^\[CONSUMER\]\s+event=shutdown\s+received_count=(\d+)\s+missed_total=(\d+)\s+crc_errors=(\d+)\s*$"
)
_CONSUMER_EVENT_RE = re.compile(r"^\[CONSUMER\]\s+event=([a-zA-Z0-9_]+)")


@dataclass(frozen=True, slots=True)
class PublishedFrame:
    """Metadata logged by the producer for one published camera frame.

    Attributes:
        frame: 1-based frame counter.
        seq: Channel sequence number assigned by ``ShmPublisher::publish``.
        size: Payload byte size.
        crc32: Lowercase 8-digit hex IEEE 802.3 CRC-32 string (``0x...``).
    """

    frame: int
    seq: int
    size: int
    crc32: str


@dataclass(frozen=True, slots=True)
class ReceivedFrame:
    """Metadata logged by the consumer for one received camera frame.

    Attributes:
        seq: Channel sequence number reported by ``ShmSubscriber``.
        missed: Number of skipped samples since the previous receive.
        size: Payload byte size.
        header_crc32: Lowercase 8-digit hex CRC-32 from the frame header.
        computed_crc32: Lowercase 8-digit hex CRC-32 recomputed over pixels.
        crc_ok: Whether the consumer validated size, sequence, and CRC-32.
    """

    seq: int
    missed: int
    size: int
    header_crc32: str
    computed_crc32: str
    crc_ok: bool


@dataclass(slots=True)
class VerificationResult:
    """Result of analyzing the producer and consumer logs.

    Attributes:
        passed: True when all verification checks succeeded.
        published_frames: Frames parsed from the producer log.
        received_frames: Frames parsed from the consumer log.
        consumer_mode: Read mode reported by the consumer (e.g. ``kLatest``).
        consumer_slots: Ring buffer slot count reported by the consumer.
        errors: Human-readable verification failures (empty when ``passed``).
    """

    passed: bool
    published_frames: list[PublishedFrame] = field(default_factory=list)
    received_frames: list[ReceivedFrame] = field(default_factory=list)
    consumer_mode: str | None = None
    consumer_slots: int | None = None
    errors: list[str] = field(default_factory=list)


def parse_producer_log(
    log_text: str,
) -> tuple[list[PublishedFrame], list[str], int | None]:
    """Parses structured stdout lines from ``shm_pubsub_producer``.

    Args:
        log_text: Raw stdout text emitted by the producer process.

    Returns:
        Tuple of ``(published_frames, event_names, shutdown_published_count)``.
    """
    frames: list[PublishedFrame] = []
    events: list[str] = []
    shutdown_count: int | None = None

    for raw_line in log_text.splitlines():
        line = raw_line.strip()
        if event_match := _PRODUCER_EVENT_RE.match(line):
            events.append(event_match.group(1))
        if frame_match := _PRODUCER_FRAME_RE.match(line):
            frames.append(
                PublishedFrame(
                    frame=int(frame_match.group(1)),
                    seq=int(frame_match.group(2)),
                    size=int(frame_match.group(3)),
                    crc32=frame_match.group(4).lower(),
                )
            )
        elif shutdown_match := _PRODUCER_SHUTDOWN_RE.match(line):
            shutdown_count = int(shutdown_match.group(1))

    return frames, events, shutdown_count


def parse_consumer_log(
    log_text: str,
) -> tuple[
    list[ReceivedFrame], list[str], str | None, int | None, tuple[int, int, int] | None
]:
    """Parses structured stdout lines from ``shm_pubsub_consumer``.

    Args:
        log_text: Raw stdout text emitted by the consumer process.

    Returns:
        Tuple of ``(received_frames, event_names, mode, slots, shutdown_tallies)``.
    """
    frames: list[ReceivedFrame] = []
    events: list[str] = []
    mode: str | None = None
    slots: int | None = None
    shutdown_tallies: tuple[int, int, int] | None = None

    for raw_line in log_text.splitlines():
        line = raw_line.strip()
        if event_match := _CONSUMER_EVENT_RE.match(line):
            events.append(event_match.group(1))
        if ready_match := _CONSUMER_READY_RE.match(line):
            mode = ready_match.group(1)
            slots = int(ready_match.group(2))
        elif frame_match := _CONSUMER_FRAME_RE.match(line):
            frames.append(
                ReceivedFrame(
                    seq=int(frame_match.group(1)),
                    missed=int(frame_match.group(2)),
                    size=int(frame_match.group(3)),
                    header_crc32=frame_match.group(4).lower(),
                    computed_crc32=frame_match.group(5).lower(),
                    crc_ok=frame_match.group(6) == "true",
                )
            )
        elif shutdown_match := _CONSUMER_SHUTDOWN_RE.match(line):
            shutdown_tallies = (
                int(shutdown_match.group(1)),
                int(shutdown_match.group(2)),
                int(shutdown_match.group(3)),
            )

    return frames, events, mode, slots, shutdown_tallies


def _check_producer_logs(
    pub_frames: Sequence[PublishedFrame],
    pub_events: Sequence[str],
    pub_shutdown: int | None,
    expected_frames: int,
    errors: list[str],
) -> dict[int, PublishedFrame]:
    """Validates producer lifecycle events, frame counts, and sequence map.

    Args:
        pub_frames: Parsed producer frame records.
        pub_events: Parsed producer lifecycle event names.
        pub_shutdown: Producer shutdown frame count.
        expected_frames: Expected total published frames.
        errors: Mutable error list to append findings to.

    Returns:
        Mapping from sequence number to published frame metadata.
    """
    required_pub_events = (
        "channel_created",
        "waiting_for_consumer",
        "consumer_ready_starting_loop",
        "shutdown",
    )
    errors.extend(
        f"Producer missing lifecycle event: {req_ev}"
        for req_ev in required_pub_events
        if req_ev not in pub_events
    )
    if len(pub_frames) != expected_frames:
        errors.append(
            f"Producer frame count mismatch: expected {expected_frames},"
            f" got {len(pub_frames)}"
        )
    if pub_shutdown != expected_frames:
        errors.append(
            f"Producer shutdown count mismatch: expected {expected_frames},"
            f" got {pub_shutdown!r}"
        )

    pub_by_seq: dict[int, PublishedFrame] = {}
    for index, pf in enumerate(pub_frames, start=1):
        if pf.frame != index or pf.seq != index:
            errors.append(
                f"Producer sequence out of order at index {index}:"
                f" frame={pf.frame}, seq={pf.seq}"
            )
        pub_by_seq[pf.seq] = pf
    return pub_by_seq


def _check_consumer_setup(
    rec_events: Sequence[str],
    mode: str | None,
    slots: int | None,
    expected_slots: int,
    errors: list[str],
) -> None:
    """Validates consumer startup lifecycle events and ring buffer configuration."""
    required_rec_events = (
        "init_waiting_for_channel",
        "ready_and_waiting",
        "shutdown",
    )
    errors.extend(
        f"Consumer missing lifecycle event: {req_ev}"
        for req_ev in required_rec_events
        if req_ev not in rec_events
    )
    if mode != "kLatest":
        errors.append(f"Consumer read mode mismatch: expected kLatest, got {mode!r}")
    if slots != expected_slots:
        errors.append(
            f"Consumer slot_count mismatch: expected {expected_slots}, got {slots!r}"
        )


def _verify_received_frames(
    rec_frames: Sequence[ReceivedFrame],
    pub_by_seq: dict[int, PublishedFrame],
    *,
    require_skips: bool,
    errors: list[str],
) -> int:
    """Validates sequence progression, missed-frame gaps, and CRC-32 consistency.

    Args:
        rec_frames: Parsed consumer received frame records.
        pub_by_seq: Published frame lookup keyed by sequence number.
        require_skips: Whether at least one 1-2 frame skip must occur.
        errors: Mutable error list to append findings to.

    Returns:
        Total missed frames tallied across received records.
    """
    if not rec_frames:
        errors.append("Consumer did not receive any frames")

    prev_seq = 0
    total_missed = 0
    saw_expected_skip = False
    for rf in rec_frames:
        if rf.seq <= prev_seq:
            errors.append(
                f"Consumer sequence not strictly increasing: {rf.seq} after {prev_seq}"
            )
        expected_missed = rf.seq - prev_seq - 1
        if rf.missed != expected_missed:
            errors.append(
                f"Consumer missed count mismatch at seq={rf.seq}:"
                f" expected {expected_missed}, got {rf.missed}"
            )
        if 1 <= rf.missed <= MAX_EXPECTED_SKIP_FRAMES:
            saw_expected_skip = True
        total_missed += rf.missed
        prev_seq = rf.seq

        _verify_frame_payload(rf, pub_by_seq, errors)

    if require_skips and (total_missed == 0 or not saw_expected_skip):
        errors.append(
            "Expected consumer to fall 1-2 frames behind producer at least once"
            f" (total_missed={total_missed},"
            f" saw_one_or_two_skip={saw_expected_skip})"
        )
    return total_missed


def _verify_frame_payload(
    rf: ReceivedFrame,
    pub_by_seq: dict[int, PublishedFrame],
    errors: list[str],
) -> None:
    """Verifies CRC-32 and payload length for a single received frame."""
    if not rf.crc_ok:
        errors.append(f"Consumer reported crc_ok=false at seq={rf.seq}")
    if rf.header_crc32 != rf.computed_crc32:
        errors.append(
            f"Consumer internal CRC mismatch at seq={rf.seq}:"
            f" header={rf.header_crc32} vs computed={rf.computed_crc32}"
        )

    matched_pub = pub_by_seq.get(rf.seq)
    if matched_pub is None:
        errors.append(f"Consumer received unknown sequence seq={rf.seq}")
        return
    if rf.computed_crc32 != matched_pub.crc32:
        errors.append(
            f"End-to-end CRC mismatch at seq={rf.seq}:"
            f" producer={matched_pub.crc32} vs consumer={rf.computed_crc32}"
        )
    if rf.size != matched_pub.size:
        errors.append(
            f"Payload size mismatch at seq={rf.seq}:"
            f" producer={matched_pub.size} vs consumer={rf.size}"
        )


def _verify_consumer_shutdown(
    rec_shutdown: tuple[int, int, int] | None,
    rec_frame_count: int,
    total_missed: int,
    errors: list[str],
) -> None:
    """Validates the consumer's shutdown summary tallies."""
    if rec_shutdown is None:
        errors.append("Consumer missing shutdown summary")
        return
    rec_count, rec_missed, rec_crc_errors = rec_shutdown
    if rec_count != rec_frame_count:
        errors.append(
            f"Consumer shutdown received_count={rec_count}"
            f" != parsed frames={rec_frame_count}"
        )
    if rec_missed != total_missed:
        errors.append(
            f"Consumer shutdown missed_total={rec_missed}"
            f" != computed missed={total_missed}"
        )
    if rec_crc_errors != 0:
        errors.append(f"Consumer shutdown reported crc_errors={rec_crc_errors}")


def verify_logs(
    producer_log: str,
    consumer_log: str,
    *,
    expected_frames: int = 20,
    expected_slots: int = 4,
    require_skips: bool = True,
) -> VerificationResult:
    """Verifies producer and consumer logs for initialization, CRC-32, and ``kLatest`` semantics.

    Args:
        producer_log: Stdout text from the producer process.
        consumer_log: Stdout text from the consumer process.
        expected_frames: Total frames the producer was configured to publish.
        expected_slots: Ring buffer slot count configured on the channel.
        require_skips: Whether the consumer must have skipped at least one frame.

    Returns:
        Populated :class:`VerificationResult`.
    """
    pub_frames, pub_events, pub_shutdown = parse_producer_log(producer_log)
    rec_frames, rec_events, mode, slots, rec_shutdown = parse_consumer_log(consumer_log)
    errors: list[str] = []

    pub_by_seq = _check_producer_logs(
        pub_frames,
        pub_events,
        pub_shutdown,
        expected_frames,
        errors,
    )
    _check_consumer_setup(rec_events, mode, slots, expected_slots, errors)
    total_missed = _verify_received_frames(
        rec_frames,
        pub_by_seq,
        require_skips=require_skips,
        errors=errors,
    )
    _verify_consumer_shutdown(rec_shutdown, len(rec_frames), total_missed, errors)

    return VerificationResult(
        passed=not errors,
        published_frames=pub_frames,
        received_frames=rec_frames,
        consumer_mode=mode,
        consumer_slots=slots,
        errors=errors,
    )


def find_or_build_binaries(
    repo_root: Path | None = None,
    preset: str = "dev-debug",
    producer_bin: Path | None = None,
    consumer_bin: Path | None = None,
) -> tuple[Path, Path]:
    """Locates or builds ``shm_pubsub_producer`` and ``shm_pubsub_consumer``.

    Args:
        repo_root: Repository root directory.
        preset: CMake preset to build if binaries are missing.
        producer_bin: Explicit path to producer executable.
        consumer_bin: Explicit path to consumer executable.

    Returns:
        Tuple of ``(producer_path, consumer_path)``.

    Raises:
        FileNotFoundError: If binaries cannot be found or built.
    """
    root = repo_root or Path(__file__).resolve().parent.parent.parent

    if producer_bin is not None and consumer_bin is not None:
        p_resolved = producer_bin.resolve()
        c_resolved = consumer_bin.resolve()
        if not p_resolved.is_file() or not c_resolved.is_file():
            msg = f"Explicit binaries not found: {p_resolved}, {c_resolved}"
            raise FileNotFoundError(msg)
        return p_resolved, c_resolved

    search_presets = [preset, "dev-debug", "dev-release"]
    for cand_preset in dict.fromkeys(search_presets):
        bin_dir = root / "build" / cand_preset / "bin"
        prod = bin_dir / "shm_pubsub_producer"
        cons = bin_dir / "shm_pubsub_consumer"
        if (
            prod.is_file()
            and os.access(prod, os.X_OK)
            and cons.is_file()
            and os.access(cons, os.X_OK)
        ):
            return prod.resolve(), cons.resolve()

    cmake_bin = shutil.which("cmake")
    if cmake_bin is not None:
        subprocess.run(
            [cmake_bin, "--preset", preset],
            cwd=root,
            capture_output=True,
            text=True,
            check=False,
        )
        subprocess.run(
            [
                cmake_bin,
                "--build",
                "--preset",
                preset,
                "--target",
                "shm_pubsub_producer",
                "shm_pubsub_consumer",
            ],
            cwd=root,
            capture_output=True,
            text=True,
            check=False,
        )
        bin_dir = root / "build" / preset / "bin"
        prod = bin_dir / "shm_pubsub_producer"
        cons = bin_dir / "shm_pubsub_consumer"
        if (
            prod.is_file()
            and os.access(prod, os.X_OK)
            and cons.is_file()
            and os.access(cons, os.X_OK)
        ):
            return prod.resolve(), cons.resolve()

    msg = f"Could not find or build shm_pubsub_producer / shm_pubsub_consumer under {root / 'build'}"
    raise FileNotFoundError(msg)


def make_ipc_names(pid: int | None = None) -> tuple[str, str]:
    """Generates portable POSIX shared-memory names (<= 30 characters).

    Args:
        pid: Optional process ID seed (defaults to ``os.getpid()``).

    Returns:
        Tuple of ``(channel_name, sync_name)``.
    """
    tag = f"{(pid if pid is not None else os.getpid()) & 0xFFFFF:x}"
    return f"/shmps_c_{tag}", f"/shmps_s_{tag}"


def _parse_int_auto_base(_type: object, tokens: Sequence[Token]) -> int:
    """Parses a decimal or hexadecimal integer CLI token (``int(s, 0)``).

    Args:
        _type: Target annotation type supplied by Cyclopts.
        tokens: Token sequence provided for the ``--seed`` parameter.

    Returns:
        Parsed integer value.
    """
    return int(tokens[0].value, 0)


@Parameter(name="*")
@dataclass(frozen=True, slots=True, kw_only=True)
class ShmPubSubOptions:
    """CLI options for the multi-process SPMC shared-memory example runner."""

    frames: Annotated[
        int,
        Parameter(help="Number of frames to publish (default: 20)."),
    ] = 20
    slots: Annotated[
        int,
        Parameter(help="Ring buffer slots, power of two (default: 4)."),
    ] = 4
    publish_sleep_ms: Annotated[
        int,
        Parameter(help="Producer sleep in ms (default: 25)."),
    ] = 25
    base_sleep_ms: Annotated[
        int,
        Parameter(help="Consumer base sleep in ms (default: 28)."),
    ] = 28
    lag_sleep_ms: Annotated[
        int,
        Parameter(help="Consumer lag sleep in ms (default: 65)."),
    ] = 65
    seed: Annotated[
        int,
        Parameter(converter=_parse_int_auto_base, help="PRNG seed."),
    ] = DEFAULT_SEED
    timeout: Annotated[
        float,
        Parameter(
            validator=validators.Number(gt=0.0),
            help="Process timeout in seconds.",
        ),
    ] = 15.0
    preset: Annotated[
        str,
        Parameter(help="CMake preset to locate/build binaries."),
    ] = "dev-debug"
    producer_bin: Annotated[
        Path | None,
        Parameter(help="Path to shm_pubsub_producer."),
    ] = None
    consumer_bin: Annotated[
        Path | None,
        Parameter(help="Path to shm_pubsub_consumer."),
    ] = None


def _build_process_commands(
    opts: ShmPubSubOptions,
    prod_path: Path,
    cons_path: Path,
    channel_name: str,
    sync_name: str,
) -> tuple[list[str], list[str]]:
    """Constructs CLI command lines for the consumer and producer processes.

    Args:
        opts: Runner options.
        prod_path: Path to producer executable.
        cons_path: Path to consumer executable.
        channel_name: Unique POSIX shared-memory channel name.
        sync_name: Unique synchronization object name.

    Returns:
        Tuple of ``(consumer_cmd, producer_cmd)``.
    """
    timeout_ms = int(opts.timeout * 1000)
    cons_cmd = [
        str(cons_path),
        "--channel",
        channel_name,
        "--sync",
        sync_name,
        "--base-sleep-ms",
        str(opts.base_sleep_ms),
        "--lag-sleep-ms",
        str(opts.lag_sleep_ms),
        "--timeout-ms",
        str(timeout_ms),
        "--cleanup",
    ]
    prod_cmd = [
        str(prod_path),
        "--channel",
        channel_name,
        "--sync",
        sync_name,
        "--frames",
        str(opts.frames),
        "--slots",
        str(opts.slots),
        "--sleep-ms",
        str(opts.publish_sleep_ms),
        "--seed",
        hex(opts.seed),
        "--timeout-ms",
        str(timeout_ms),
    ]
    return cons_cmd, prod_cmd


def _print_verification_summary(
    channel_name: str,
    sync_name: str,
    verdict: VerificationResult,
) -> None:
    """Writes a human-readable verification summary table to stdout."""
    sys.stdout.write("\n--- Verification Summary ---\n")
    total_missed = sum(rf.missed for rf in verdict.received_frames)
    sys.stdout.write(f"  Channel / Sync     : {channel_name} / {sync_name}\n")
    sys.stdout.write(f"  Ring Buffer Slots  : {verdict.consumer_slots}\n")
    sys.stdout.write(f"  Consumer Read Mode : {verdict.consumer_mode}\n")
    sys.stdout.write(f"  Frames Published   : {len(verdict.published_frames)}\n")
    sys.stdout.write(f"  Frames Received    : {len(verdict.received_frames)}\n")
    sys.stdout.write(f"  Frames Skipped     : {total_missed}\n")


def run_integration_example(config: ShmPubSubOptions | None = None) -> int:
    """Runs the consumer and producer processes and validates their logs.

    Args:
        config: Optional execution options (defaults to ``ShmPubSubOptions()``).

    Returns:
        Exit code: ``0`` if the integration test succeeded, ``1`` otherwise.
    """
    opts = config if config is not None else ShmPubSubOptions()
    logger = _get_stderr_logger()

    prod_path, cons_path = find_or_build_binaries(
        preset=opts.preset,
        producer_bin=opts.producer_bin,
        consumer_bin=opts.consumer_bin,
    )
    channel_name, sync_name = make_ipc_names()
    cons_cmd, prod_cmd = _build_process_commands(
        opts, prod_path, cons_path, channel_name, sync_name
    )

    sys.stdout.write(
        f"==> Launching Consumer process first (PID-isolated): {' '.join(cons_cmd)}\n"
    )
    cons_proc = subprocess.Popen(
        cons_cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    # Brief pause so the consumer process initializes its rendezvous wait before
    # the producer process is spawned.
    time.sleep(0.05)

    sys.stdout.write(f"==> Launching Producer process: {' '.join(prod_cmd)}\n")
    prod_proc = subprocess.Popen(
        prod_cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    try:
        prod_out, prod_err = prod_proc.communicate(timeout=opts.timeout)
        cons_out, cons_err = cons_proc.communicate(timeout=opts.timeout)
    except subprocess.TimeoutExpired:
        prod_proc.kill()
        cons_proc.kill()
        prod_proc.communicate()
        cons_proc.communicate()
        logger.error("INTEGRATION TEST FAILED: Producer or consumer timed out.")
        return 1

    sys.stdout.write("\n--- Producer Log ---\n")
    sys.stdout.write(prod_out)
    if prod_err:
        sys.stderr.write(prod_err)

    sys.stdout.write("\n--- Consumer Log ---\n")
    sys.stdout.write(cons_out)
    if cons_err:
        sys.stderr.write(cons_err)

    if prod_proc.returncode != 0 or cons_proc.returncode != 0:
        logger.error(
            "INTEGRATION TEST FAILED: non-zero exit code",
            producer_returncode=prod_proc.returncode,
            consumer_returncode=cons_proc.returncode,
        )
        return 1

    verdict = verify_logs(
        prod_out,
        cons_out,
        expected_frames=opts.frames,
        expected_slots=opts.slots,
        require_skips=True,
    )
    _print_verification_summary(channel_name, sync_name, verdict)

    if not verdict.passed:
        for err in verdict.errors:
            logger.error("Verification error", detail=err)
        logger.error("INTEGRATION TEST FAILED")
        return 1

    sys.stdout.write("  CRC-32 Verification: 100% matched across all received frames\n")
    sys.stdout.write("\nINTEGRATION TEST PASSED\n")
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    """CLI entry point for the multi-process SPMC shared-memory example runner.

    Args:
        argv: Optional CLI argument list (defaults to ``sys.argv[1:]``).

    Returns:
        Exit code: ``0`` on pass, ``1`` on failure.
    """
    app = cyclopts.App(
        name="run_shm_pubsub_example",
        help=__doc__,
        help_on_error=True,
        version_flags=[],
    )
    app.default(run_integration_example)
    tokens = list(argv) if argv is not None else sys.argv[1:]
    result = app(tokens, exit_on_error=True, result_action="return_value")
    return result if isinstance(result, int) else 0


if __name__ == "__main__":
    sys.exit(main())
