#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""story-0603 — `RawDevice`'s Linux half, run against a real block device.

Every CI run compiles `RawDevicePosix.cpp` and `NativeIoPosix.cpp`; until this
script nothing had executed them past the line that turns a missing path into
`kNotFound`. The two ioctls, the open of a device that exists and every
successful read had run zero times, on any platform — which also left
`ADR-0011`'s read-only guarantee proven for `ImageFileDevice` alone.

This is the order the pass runs in. Root, on a Linux host with `losetup`; the
WSL2 workbench, see `.claude/skills/wsl-bench/SKILL.md`:

    wsl.exe -d Debian -u root -- bash -lc \\
      'python3 /mnt/d/Projects/data-recovery/tools/loopdev/verify_loop_device.py'

It is not a CI test and is not meant to become one: runners hand out no block
devices, and the negative case needs a user who is genuinely refused, which a
runner with passwordless sudo can only pantomime.

Exit status is `0`, or the number of checks that did not pass, or `ABORTED` —
which means the pass stopped and the checks after it never ran. Read it from the
process; an `echo $?` on the far side of `wsl.exe` loses it.
"""
from __future__ import annotations

import argparse
import os
import sys
import traceback
from pathlib import Path

import checks
import loop_device
import runs
from bench import SOURCE_NAMES, Bench, BenchError, prepare
from ledger import Ledger

# Higher than any plausible check count, so "the pass did not finish" cannot be
# read as "this many checks failed".
ABORTED = 70


def _on_the_512_byte_attachment(bench: Bench, ledger: Ledger, user: str, device: str) -> None:
    undelete, disk = bench.tools.undelete, bench.disk
    listings = runs.listings_of(undelete, disk, device)
    checks.check_listing(ledger, listings)
    checks.check_kernel_lengths(ledger, listings, device)
    recovered = runs.written_by(
        undelete, disk, device, bench.destinations("recover"), "--partition", "1"
    )
    checks.check_artifacts(ledger, recovered)
    checks.check_session(ledger, recovered)
    checks.check_manifest(ledger, recovered, disk, device)
    was_refused = runs.refused(device, user)
    attempt = runs.run_tool(undelete, "--source", device, "--list-partitions", as_user=user)
    checks.check_unprivileged(
        ledger, attempt, was_refused=was_refused, user=user, device=device
    )


def _at_4kn(bench: Bench, ledger: Ledger, device: str) -> None:
    carved = runs.written_by(bench.tools.carve, bench.disk, device, bench.destinations("carve"))
    checks.check_4kn_carve(ledger, carved, loop_device.sector_size(device))


def _read_only(bench: Bench, ledger: Ledger, device: str, refuses_writes: bool) -> None:
    recovered = runs.written_by(
        bench.tools.undelete,
        bench.disk,
        device,
        bench.destinations("read-only"),
        "--partition",
        "1",
    )
    checks.check_read_only(ledger, recovered, refuses_writes)


def _damaged_gpt(bench: Bench, ledger: Ledger, device: str) -> None:
    gpt = runs.listings_of(bench.tools.undelete, bench.damaged_gpt, device)
    checks.check_backup_header(ledger, gpt)


def run_pass(bench: Bench, ledger: Ledger, unprivileged_user: str) -> None:
    disk = bench.disk
    with loop_device.attached(disk, partition_scan=True) as device:
        print(
            f"# {device} <- {disk}, node {loop_device.node_mode(device)}, "
            f"{loop_device.size_bytes(device)} bytes, "
            f"{loop_device.sector_size(device)}-byte sectors"
        )
        _on_the_512_byte_attachment(bench, ledger, unprivileged_user, device)
    with loop_device.attached(disk, sector_size=checks.FOUR_KN_SECTOR) as device:
        print(f"# {device} <- {disk}, {loop_device.sector_size(device)}-byte sectors")
        _at_4kn(bench, ledger, device)
    with loop_device.attached(disk, partition_scan=True, read_only=True) as device:
        refuses_writes = loop_device.is_read_only(device)
        print(f"# {device} <- {disk}, read-only: {refuses_writes}")
        _read_only(bench, ledger, device, refuses_writes)
    with loop_device.attached(bench.damaged_gpt) as device:
        print(f"# {device} <- {bench.damaged_gpt}, primary GPT header wiped")
        _damaged_gpt(bench, ledger, device)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scratch", type=Path, default=Path("/var/tmp/revenant-loopdev"))
    parser.add_argument("--unprivileged-user", default="nobody")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    ledger = Ledger(expected=checks.EVERY_CHECK)
    try:
        if os.geteuid() != 0:
            raise BenchError("losetup needs root; run under `wsl.exe -d Debian -u root`")
        bench = prepare(args.scratch)
        # ADR-0011's other half: whatever the checks do to these devices, the
        # bytes underneath them must be the ones we started with.
        before = runs.digests_of(bench.sources())
        run_pass(bench, ledger, args.unprivileged_user)
        checks.check_sources_unchanged(
            ledger, SOURCE_NAMES, before, runs.digests_of(bench.sources())
        )
        ledger.finish()
    except Exception:
        # Anything at all: the pass stopped, and whatever came after it never
        # ran. The traceback goes with it — this script exists to diagnose an
        # unfamiliar machine, and a bare `IndexError` diagnoses nothing.
        print(f"ABORT         the pass did not finish\n{traceback.format_exc()}")
        return ABORTED

    print(f"\n{ledger.failures} check(s) did not pass")
    return ledger.failures


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
