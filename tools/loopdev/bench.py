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


# Every backing file the pass attaches. Stated here so the check that says
# "both sources" can be held to watching each of them by name.
SOURCE_NAMES = ("the MBR disk", "the damaged GPT")


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

    def destinations(self, stage: str) -> tuple[Path, Path]:
        """Where the image run and the device run of one stage write."""
        return self.work / f"{stage}-image", self.work / f"{stage}-device"

    def sources(self) -> dict[str, Path]:
        """Every backing file the pass means to attach, by the name a verdict uses."""
        return dict(zip(SOURCE_NAMES, (self.disk, self.damaged_gpt), strict=True))

    def name_of(self, backing: Path) -> str:
        """What a verdict calls this backing file.

        A path the bench does not know still gets a name, so a fixture attached
        without being declared shows up as a source that was never digested
        rather than passing unnoticed.
        """
        for name, path in self.sources().items():
            if path == backing:
                return name
        return f"an undeclared source ({backing})"


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
            raise BenchError(f"{' '.join(command[:3])} failed:\n{finished.stderr}")
    return Tools(
        undelete=directory / "src/revenant-undelete",
        carve=directory / "src/revenant-carve",
        imagegen=directory / "tools/imagegen/revenant-imagegen",
    )


def _mbr_disk(imagegen: Path, work: Path) -> Path:
    """The MBR disk carrying the NTFS, FAT32, exFAT and ext4 fixtures."""
    disk = work / "disk.img"
    made = subprocess.run(
        [str(imagegen), "disk", str(disk)], capture_output=True, text=True, check=False
    )
    if made.returncode != 0:
        raise BenchError(f"revenant-imagegen disk failed: {made.stderr}")
    return disk


def _damaged_gpt(work: Path) -> Path:
    """The checked-in GPT, with its primary header wiped.

    What survives is the backup copy in the last sector, which is only findable
    from the device's own size — so reading it exercises `BLKGETSIZE64`.
    """
    damaged = work / "gpt-wiped.img"
    image = bytearray(GPT_FIXTURE.read_bytes())
    image[PRIMARY_GPT_HEADER] = bytes(PRIMARY_GPT_HEADER.stop - PRIMARY_GPT_HEADER.start)
    damaged.write_bytes(image)
    return damaged


def prepare(scratch: Path) -> Bench:
    work = scratch / "work"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    # The unprivileged check runs one of these binaries as a user who owns none
    # of this, and has to be able to reach it.
    scratch.chmod(0o755)
    tools = build(scratch)
    return Bench(
        tools=tools,
        work=work,
        disk=_mbr_disk(tools.imagegen, work),
        damaged_gpt=_damaged_gpt(work),
    )
