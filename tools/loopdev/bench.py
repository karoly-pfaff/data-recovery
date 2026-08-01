#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What story-0603's pass is run against, and what it takes to have it.

The three binaries and the two fixtures — everything that has to exist before a
single loop device is attached. Kept apart from the pass itself because
preparing a bench and running one are not the same job, and only one of them
can fail before any check has an opinion.
"""
from __future__ import annotations

import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]

# The fixture whose backup GPT header the pass reads from the end of the device,
# at an address computed from what `BLKGETSIZE64` answered.
GPT_FIXTURE = REPOSITORY / "tests/fuzz/corpus/GptFuzz/gpt-disk.bin"

# One 512-byte sector at LBA 1: where a GPT keeps its primary header, and what
# the damaged fixture has wiped.
PRIMARY_GPT_HEADER = slice(512, 1024)


class BenchError(RuntimeError):
    """The bench could not be built, so no check ever got an opinion."""


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
        raise BenchError("revenant-imagegen disk failed")
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


