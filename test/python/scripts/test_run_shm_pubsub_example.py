"""Tests for scripts/examples/run_shm_pubsub_example.py."""

from __future__ import annotations

import time
from typing import TYPE_CHECKING

import pytest

from scripts.examples import run_shm_pubsub_example as runner


if TYPE_CHECKING:
    from pathlib import Path


SAMPLE_PRODUCER_LOG = """\
[PRODUCER] event=channel_created channel=/shmps_c_1234 slots=4 slot_size=16416
[PRODUCER] event=waiting_for_consumer sync=/shmps_s_1234
[PRODUCER] event=consumer_ready_starting_loop total_frames=5
[PRODUCER] event=frame_published frame=1 seq=1 size=16416 crc32=0x11111111
[PRODUCER] event=frame_published frame=2 seq=2 size=16416 crc32=0x22222222
[PRODUCER] event=frame_published frame=3 seq=3 size=16416 crc32=0x33333333
[PRODUCER] event=frame_published frame=4 seq=4 size=16416 crc32=0x44444444
[PRODUCER] event=frame_published frame=5 seq=5 size=16416 crc32=0x55555555
[PRODUCER] event=shutdown published_count=5
"""

SAMPLE_CONSUMER_LOG = """\
[CONSUMER] event=init_waiting_for_channel channel=/shmps_c_1234 sync=/shmps_s_1234
[CONSUMER] event=ready_and_waiting mode=kLatest slots=4 slot_size=16416
[CONSUMER] event=frame_received seq=1 missed=0 size=16416 header_crc32=0x11111111 computed_crc32=0x11111111 crc_ok=true
[CONSUMER] event=frame_received seq=3 missed=1 size=16416 header_crc32=0x33333333 computed_crc32=0x33333333 crc_ok=true
[CONSUMER] event=frame_received seq=5 missed=1 size=16416 header_crc32=0x55555555 computed_crc32=0x55555555 crc_ok=true
[CONSUMER] event=shutdown received_count=3 missed_total=2 crc_errors=0
"""


# Tier 1: canonical behavior


def test_verify_logs_passes_on_valid_logs() -> None:
    """Valid producer and consumer logs pass all verification checks."""
    verdict = runner.verify_logs(
        SAMPLE_PRODUCER_LOG,
        SAMPLE_CONSUMER_LOG,
        expected_frames=5,
        expected_slots=4,
    )
    assert verdict.passed is True
    assert verdict.errors == []
    assert verdict.consumer_mode == 'kLatest'
    assert verdict.consumer_slots == 4
    assert len(verdict.published_frames) == 5
    assert [f.seq for f in verdict.received_frames] == [1, 3, 5]


def test_make_ipc_names_fits_posix_limit() -> None:
    """Generated IPC names start with '/' and fit within the 30-character macOS limit."""
    channel_name, sync_name = runner.make_ipc_names(0xABCDE)
    assert channel_name.startswith('/')
    assert sync_name.startswith('/')
    assert channel_name != sync_name
    assert len(channel_name) <= 30
    assert len(sync_name) <= 30


@pytest.mark.integration
def test_end_to_end_shm_pubsub_example(capsys: pytest.CaptureFixture[str]) -> None:
    """Runs the compiled C++ producer and consumer processes end-to-end."""
    code = runner.main([
        '--frames',
        '20',
        '--slots',
        '4',
        '--publish-sleep-ms',
        '15',
        '--base-sleep-ms',
        '18',
        '--lag-sleep-ms',
        '45',
    ])
    captured = capsys.readouterr()
    assert code == 0, captured.err
    assert 'INTEGRATION TEST PASSED' in captured.out


# Tier 2: boundary and error cases


def test_verify_logs_detects_crc_mismatch() -> None:
    """A corrupted CRC-32 between producer and consumer fails verification."""
    bad_consumer = SAMPLE_CONSUMER_LOG.replace('computed_crc32=0x33333333', 'computed_crc32=0xdeadbeef')
    verdict = runner.verify_logs(
        SAMPLE_PRODUCER_LOG,
        bad_consumer,
        expected_frames=5,
        expected_slots=4,
    )
    assert verdict.passed is False
    assert any('CRC mismatch at seq=3' in err for err in verdict.errors)


def test_verify_logs_detects_wrong_missed_accounting_and_mode() -> None:
    """Incorrect missed count or non-kLatest mode fails verification."""
    bad_consumer = SAMPLE_CONSUMER_LOG.replace('mode=kLatest', 'mode=kInOrder').replace('missed=1', 'missed=0', 1)
    verdict = runner.verify_logs(
        SAMPLE_PRODUCER_LOG,
        bad_consumer,
        expected_frames=5,
        expected_slots=4,
    )
    assert verdict.passed is False
    assert any('Consumer read mode mismatch' in err for err in verdict.errors)
    assert any('Consumer missed count mismatch at seq=3' in err for err in verdict.errors)


def test_verify_logs_detects_no_skips_when_required() -> None:
    """When require_skips is True, receiving every frame with missed=0 fails."""
    no_skip_consumer = """\
[CONSUMER] event=init_waiting_for_channel channel=/shmps_c_1234 sync=/shmps_s_1234
[CONSUMER] event=ready_and_waiting mode=kLatest slots=4 slot_size=16416
[CONSUMER] event=frame_received seq=1 missed=0 size=16416 header_crc32=0x11111111 computed_crc32=0x11111111 crc_ok=true
[CONSUMER] event=shutdown received_count=1 missed_total=0 crc_errors=0
"""
    verdict = runner.verify_logs(
        SAMPLE_PRODUCER_LOG,
        no_skip_consumer,
        expected_frames=5,
        expected_slots=4,
        require_skips=True,
    )
    assert verdict.passed is False
    assert any('Expected consumer to fall 1-2 frames behind' in err for err in verdict.errors)


def test_find_or_build_binaries_raises_on_missing_explicit_paths(tmp_path: Path) -> None:
    """Explicit binary paths that do not exist raise FileNotFoundError."""
    with pytest.raises(FileNotFoundError, match='Explicit binaries not found'):
        runner.find_or_build_binaries(
            producer_bin=tmp_path / 'missing_prod',
            consumer_bin=tmp_path / 'missing_cons',
        )


# Tier 3: invariants


def test_received_plus_missed_equals_final_sequence() -> None:
    """Total received count plus total missed count equals the last received sequence."""
    verdict = runner.verify_logs(
        SAMPLE_PRODUCER_LOG,
        SAMPLE_CONSUMER_LOG,
        expected_frames=5,
        expected_slots=4,
    )
    assert verdict.passed is True
    total_received = len(verdict.received_frames)
    total_missed = sum(rf.missed for rf in verdict.received_frames)
    assert total_received + total_missed == verdict.received_frames[-1].seq


# Tier 4: stress


@pytest.mark.stress
def test_large_log_verification_scales() -> None:
    """Verifying a 2,000-frame producer/consumer run completes quickly."""
    count = 2000
    pub_lines = [
        '[PRODUCER] event=channel_created channel=/shmps_c slots=4 slot_size=16416',
        '[PRODUCER] event=waiting_for_consumer sync=/shmps_s',
        f'[PRODUCER] event=consumer_ready_starting_loop total_frames={count}',
    ]
    cons_lines = [
        '[CONSUMER] event=init_waiting_for_channel channel=/shmps_c sync=/shmps_s',
        '[CONSUMER] event=ready_and_waiting mode=kLatest slots=4 slot_size=16416',
    ]

    prev_seq = 0
    rec_count = 0
    missed_total = 0
    for seq in range(1, count + 1):
        crc = f'0x{seq:08x}'
        pub_lines.append(f'[PRODUCER] event=frame_published frame={seq} seq={seq} size=16416 crc32={crc}')
        if seq % 2 == 1:
            missed = seq - prev_seq - 1
            prev_seq = seq
            rec_count += 1
            missed_total += missed
            cons_lines.append(
                f'[CONSUMER] event=frame_received seq={seq} missed={missed} size=16416 '
                f'header_crc32={crc} computed_crc32={crc} crc_ok=true'
            )

    pub_lines.append(f'[PRODUCER] event=shutdown published_count={count}')
    cons_lines.append(f'[CONSUMER] event=shutdown received_count={rec_count} missed_total={missed_total} crc_errors=0')

    start = time.perf_counter()
    verdict = runner.verify_logs(
        '\n'.join(pub_lines),
        '\n'.join(cons_lines),
        expected_frames=count,
        expected_slots=4,
    )
    elapsed = time.perf_counter() - start
    assert verdict.passed is True
    assert elapsed < 2.0
