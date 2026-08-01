#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""story-0603 — `RawDevice`'s Linux half, run against a real block device.

Every CI run compiles `RawDevicePosix.cpp` and `NativeIoPosix.cpp`; until this
script nothing had executed them past the line that turns a missing path into
`kNotFound`. The two ioctls, the open of a device that exists and every
successful read had run zero times, on any platform — which also left
[ADR-0011](../../docs/architecture/adr/adr-0011-two-halves-of-the-read-only-guarantee.md)'s
read-only guarantee proven for `ImageFileDevice` alone.

This sets the pass up — binaries, fixtures, attachments — and runs the checks in
`checks.py` in order. Root, on a Linux host with `losetup`; the WSL2 workbench,
see `.claude/skills/wsl-bench/SKILL.md`:

    wsl.exe -d Debian -u root -- bash -lc \\
      'python3 /mnt/d/Projects/data-recovery/tools/loopdev/verify_loop_device.py'

It is not a CI test and is not meant to become one: runners hand out no block
devices, and the negative case needs a user who is genuinely refused, which a
runner with passwordless sudo can only pantomime.

Exit status is the number of checks that did not pass, except `ABORTED`, which
means the pass did not finish and the checks after the failure never ran. Read
it from the process — an `echo $?` on the far side of `wsl.exe` loses it.
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import checks
import loop_device
import runs
from bench import Bench, BenchError, prepare
from ledger import Ledger

# Higher than any plausible check count, so "the pass did not finish" cannot be
# read as "this many checks failed".
ABORTED = 70

# Every check the pass records — the nine in `run_pass` and the source digest.
# Stated so a check that silently stops being called is a failure rather than a
# shorter list of PASS lines.
CHECK_COUNT = 10


def _on_the_512_byte_attachment(bench: Bench, ledger: Ledger, user: str, device: str) -> None:
    undelete, disk = bench.tools.undelete, bench.disk
    listings = runs.listings_of(undelete, disk, device)
    checks.check_listing(ledger, listings)
    checks.check_kernel_lengths(ledger, listings, device)
    recovered = runs.written_by(
        undelete,
        disk,
        device,
        (bench.work / "recover-image", bench.work / "recover-device"),
        "--partition",
        "1",
    )
    checks.check_artifacts(ledger, recovered)
    checks.check_session(ledger, recovered)
    checks.check_manifest(ledger, recovered, disk, device)
    checks.check_unprivileged(ledger, undelete, device, user)


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
        carved = runs.written_by(
            bench.tools.carve,
            disk,
            device,
            (bench.work / "carve-image", bench.work / "carve-4kn"),
        )
        checks.check_4kn_carve(ledger, carved, loop_device.sector_size(device))
    with loop_device.attached(disk, partition_scan=True, read_only=True) as device:
        print(f"# {device} <- {disk}, attached read-only")
        checks.check_read_only(ledger, runs.listings_of(bench.tools.undelete, disk, device))
    with loop_device.attached(bench.damaged_gpt) as device:
        print(f"# {device} <- {bench.damaged_gpt}, primary GPT header wiped")
        gpt = runs.listings_of(bench.tools.undelete, bench.damaged_gpt, device)
        checks.check_backup_header(ledger, gpt)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scratch", type=Path, default=Path("/var/tmp/revenant-loopdev"))
    parser.add_argument("--unprivileged-user", default="nobody")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    ledger = Ledger(expected=CHECK_COUNT)
    try:
        if os.geteuid() != 0:
            raise BenchError("losetup needs root; run under `wsl.exe -d Debian -u root`")
        bench = prepare(args.scratch)
        # ADR-0011's other half: whatever the checks do to the device, the bytes
        # underneath it must be the ones we started with.
        before = runs.digest_of(bench.disk)
        run_pass(bench, ledger, args.unprivileged_user)
        after = runs.digest_of(bench.disk)
    except (BenchError, loop_device.LoopError, OSError) as failure:
        print(f"ABORT         the pass did not finish: {failure}")
        return ABORTED
    checks.check_source_unchanged(ledger, before, after)
    ledger.finish()

    print(f"\n{ledger.failures} check(s) did not pass")
    return ledger.failures


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
