#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""story-0603 — `RawDevice`'s Linux half, run against a real block device.

Every CI run compiles `RawDevicePosix.cpp` and `NativeIoPosix.cpp`; until this
script nothing had executed them past the line that turns a missing path into
`kNotFound`. The two ioctls, the open of a device that exists and every
successful read had run zero times, on any platform.

This sets the pass up — binaries, fixtures, attachments — and runs the checks in
`checks.py` in order. Root, on a Linux host with `losetup`; the WSL2 workbench,
see `.claude/skills/wsl-bench/SKILL.md`:

    wsl.exe -d Debian -u root -- bash -lc \\
      'python3 /mnt/d/Projects/data-recovery/tools/loopdev/verify_loop_device.py'

It is not a CI test and is not meant to become one: runners hand out no block
devices, and the negative case needs a user who is genuinely refused, which a
runner with passwordless sudo can only pantomime. Exit status is the number of
checks that did not pass, so 0 is the green pass the story records — read it
from the process, not from an `echo $?` on the far side of `wsl.exe`, which
loses it.
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
import loop_device

REPOSITORY = Path(__file__).resolve().parents[2]

# The fixture whose backup GPT header the pass reads from the end of the device,
# at an address computed from what `BLKGETSIZE64` answered.
GPT_FIXTURE = REPOSITORY / "tests/fuzz/corpus/GptFuzz/gpt-disk.bin"


@dataclass(frozen=True)
class Tools:
    undelete: Path
    carve: Path
    imagegen: Path


def build(scratch: Path) -> Tools:
    """The three binaries, without a preset.

    Every preset pins the vcpkg toolchain the workbench does not have, and with
    the tests off the tree needs no dependency at all — `gtest` is
    `vcpkg.json`'s only entry.
    """
    directory = scratch / "build"
    configure = [
        "cmake", "-S", str(REPOSITORY), "-B", str(directory), "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DREVENANT_BUILD_TESTS=OFF",
    ]  # fmt: skip
    targets = ["revenant-carve", "revenant-undelete", "revenant-imagegen"]
    compile_them = ["cmake", "--build", str(directory), "--target", *targets]
    for command in (configure, compile_them):
        if subprocess.run(command, capture_output=True, check=False).returncode != 0:
            raise SystemExit(f"ABORT         {' '.join(command[:3])} failed")
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
    if subprocess.run([str(tools.imagegen), "disk", str(disk)], check=False).returncode != 0:
        raise SystemExit("ABORT         revenant-imagegen disk failed")
    damaged_gpt = work / "gpt-wiped.img"
    image = bytearray(GPT_FIXTURE.read_bytes())
    image[512:1024] = bytes(512)
    damaged_gpt.write_bytes(image)
    return disk, damaged_gpt


def prepare(scratch: Path) -> tuple[Tools, Path, Path, Path]:
    work = scratch / "work"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    # The unprivileged check runs one of these binaries as a user who owns none
    # of this, and needs to be able to reach it.
    scratch.chmod(0o755)
    tools = build(scratch)
    disk, damaged_gpt = fixtures(tools, work)
    return tools, work, disk, damaged_gpt


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scratch", type=Path, default=Path("/var/tmp/revenant-loopdev"))
    parser.add_argument("--unprivileged-user", default="nobody")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if os.geteuid() != 0:
        raise SystemExit("ABORT         losetup needs root; run under `wsl.exe -d Debian -u root`")
    tools, work, disk, damaged_gpt = prepare(args.scratch)

    ledger = checks.Ledger()
    with loop_device.attached(disk, partition_scan=True) as device:
        print(f"# {device} <- {disk}, node {loop_device.node_mode(device)}, "
              f"{loop_device.size_bytes(device)} bytes, "
              f"{loop_device.sector_size(device)}-byte sectors")  # fmt: skip
        checks.check_listing(ledger, tools.undelete, disk, device)
        checks.check_recovery(ledger, tools.undelete, disk, device, work)
        checks.check_unprivileged(ledger, tools.undelete, device, args.unprivileged_user)
    with loop_device.attached(disk, sector_size=4096) as device:
        print(f"# {device} <- {disk}, {loop_device.sector_size(device)}-byte sectors")
        checks.check_4kn_carve(ledger, tools.carve, disk, device, work)
    with loop_device.attached(damaged_gpt) as device:
        print(f"# {device} <- {damaged_gpt}, primary GPT header wiped")
        checks.check_backup_header(ledger, tools.undelete, damaged_gpt, device)

    print(f"\n{ledger.failures} check(s) did not pass")
    return ledger.failures


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
