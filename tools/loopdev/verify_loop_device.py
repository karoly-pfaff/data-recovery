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
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

import checks
import identity
import loop_device
from ledger import Ledger

REPOSITORY = Path(__file__).resolve().parents[2]

# The fixture whose backup GPT header the pass reads from the end of the device,
# at an address computed from what `BLKGETSIZE64` answered.
GPT_FIXTURE = REPOSITORY / "tests/fuzz/corpus/GptFuzz/gpt-disk.bin"

# One 512-byte sector at LBA 1: where a GPT keeps its primary header, and what
# the damaged fixture has wiped.
PRIMARY_GPT_HEADER = slice(512, 1024)

# Higher than any plausible check count, so "the pass did not finish" cannot be
# read as "this many checks failed".
ABORTED = 70

# Every check the pass records — the nine in `run_pass` and the source digest.
# Stated so a check that silently stops being called is a failure rather than a
# shorter list of PASS lines.
CHECK_COUNT = 10


@dataclass(frozen=True)
class Tools:
    undelete: Path
    carve: Path
    imagegen: Path


@dataclass(frozen=True)
class Bench:
    tools: Tools
    work: Path
    disk: Path
    damaged_gpt: Path


def build(scratch: Path) -> Tools:
    """The three binaries, without a preset.

    Every preset pins the vcpkg toolchain the workbench does not have, and with
    the tests off the tree needs no dependency at all — `gtest` is
    `vcpkg.json`'s only entry.
    """
    directory = scratch / "build"
    configure = [
        "cmake",
        "-S",
        str(REPOSITORY),
        "-B",
        str(directory),
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DREVENANT_BUILD_TESTS=OFF",
    ]
    targets = ["revenant-carve", "revenant-undelete", "revenant-imagegen"]
    compile_them = ["cmake", "--build", str(directory), "--target", *targets]
    for command in (configure, compile_them):
        finished = subprocess.run(command, capture_output=True, text=True, check=False)
        if finished.returncode != 0:
            raise SystemExit(f"ABORT         {' '.join(command[:3])} failed:\n{finished.stderr}")
    return Tools(
        undelete=directory / "src/revenant-undelete",
        carve=directory / "src/revenant-carve",
        imagegen=directory / "tools/imagegen/revenant-imagegen",
    )


def fixtures(tools: Tools, work: Path) -> tuple[Path, Path]:
    """The MBR disk, and a GPT whose primary header has been wiped.

    Both live on the distro's own filesystem rather than `/mnt/d`: whether
    `losetup` humors a backing file on a 9p mount is a second experiment this
    story does not need.
    """
    disk = work / "disk.img"
    made = subprocess.run([str(tools.imagegen), "disk", str(disk)], check=False)
    if made.returncode != 0:
        raise SystemExit("ABORT         revenant-imagegen disk failed")
    damaged_gpt = work / "gpt-wiped.img"
    image = bytearray(GPT_FIXTURE.read_bytes())
    image[PRIMARY_GPT_HEADER] = bytes(PRIMARY_GPT_HEADER.stop - PRIMARY_GPT_HEADER.start)
    damaged_gpt.write_bytes(image)
    return disk, damaged_gpt


def prepare(scratch: Path) -> Bench:
    work = scratch / "work"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    # The unprivileged check runs one of these binaries as a user who owns none
    # of this, and has to be able to reach it.
    scratch.chmod(0o755)
    tools = build(scratch)
    disk, damaged_gpt = fixtures(tools, work)
    return Bench(tools=tools, work=work, disk=disk, damaged_gpt=damaged_gpt)


def run_pass(bench: Bench, ledger: Ledger, unprivileged_user: str) -> None:
    tools, disk = bench.tools, bench.disk
    with loop_device.attached(disk, partition_scan=True) as device:
        print(
            f"# {device} <- {disk}, node {loop_device.node_mode(device)}, "
            f"{loop_device.size_bytes(device)} bytes, "
            f"{loop_device.sector_size(device)}-byte sectors"
        )
        checks.check_listing(ledger, tools.undelete, disk, device)
        checks.check_recovery(ledger, tools.undelete, disk, device, bench.work)
        checks.check_unprivileged(ledger, tools.undelete, device, unprivileged_user)
    with loop_device.attached(disk, sector_size=checks.FOUR_KN_SECTOR) as device:
        print(f"# {device} <- {disk}, {loop_device.sector_size(device)}-byte sectors")
        checks.check_4kn_carve(ledger, tools.carve, disk, device, bench.work)
    with loop_device.attached(disk, partition_scan=True, read_only=True) as device:
        print(f"# {device} <- {disk}, attached read-only")
        checks.check_read_only(ledger, tools.undelete, disk, device)
    with loop_device.attached(bench.damaged_gpt) as device:
        print(f"# {device} <- {bench.damaged_gpt}, primary GPT header wiped")
        checks.check_backup_header(ledger, tools.undelete, bench.damaged_gpt, device)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scratch", type=Path, default=Path("/var/tmp/revenant-loopdev"))
    parser.add_argument("--unprivileged-user", default="nobody")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if os.geteuid() != 0:
        raise SystemExit("ABORT         losetup needs root; run under `wsl.exe -d Debian -u root`")
    bench = prepare(args.scratch)

    # ADR-0011's other half: whatever the checks below do to the device, the
    # bytes underneath it must be the ones we started with.
    before = checks.digest_of(bench.disk)
    ledger = Ledger(expected=CHECK_COUNT)
    try:
        run_pass(bench, ledger, args.unprivileged_user)
    except (loop_device.LoopError, OSError) as failure:
        print(f"ABORT         the pass stopped before it finished: {failure}")
        return ABORTED
    ledger.record(
        "the source is byte-for-byte what it was before the pass",
        identity.unchanged_problems(before, checks.digest_of(bench.disk), what="the backing file"),
    )
    ledger.finish()

    print(f"\n{ledger.failures} check(s) did not pass")
    return ledger.failures


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
