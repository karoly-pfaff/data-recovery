#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""story-0603 — `RawDevice`'s Linux half, run against a real block device.

Every CI run compiles `RawDevicePosix.cpp` and `NativeIoPosix.cpp`; until this
script nothing had executed them past the line that turns a missing path into
`kNotFound`. The two ioctls, the open of a device that exists and every
successful read had run zero times, on any platform.

This attaches the synthetic fixtures to `/dev/loopN` and runs the whole stack
against them, holding each answer against the image-file run over the same
bytes. Root, on a Linux host with `losetup` — the WSL2 workbench, see
`.claude/skills/wsl-bench/SKILL.md`:

    wsl.exe -d Debian -u root -- bash -lc \\
      'python3 /mnt/d/Projects/data-recovery/tools/loopdev/verify_loop_device.py'

It is not a CI test and is not meant to become one: runners hand out no block
devices, and the negative case needs a user who is genuinely refused, which a
runner with passwordless sudo can only pantomime. Exit status is the number of
checks that did not pass, so 0 is the green pass the story records.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

import identity
import loop_device

REPOSITORY = Path(__file__).resolve().parents[2]

# The fixture whose backup GPT header the pass reads from the end of the device,
# at an address computed from what `BLKGETSIZE64` answered.
GPT_FIXTURE = REPOSITORY / "tests/fuzz/corpus/GptFuzz/gpt-disk.bin"

# The sentence `RunSummary.cpp` promises an operator who cannot open the source.
# Asserted verbatim: M4 wrote it, and until now nothing had ever produced it
# from an actual refusal.
PERMISSION_SENTENCE = (
    "the operating system refused to open the source: reading a whole disk or a"
    " mounted volume needs administrator (Windows) or root/disk-group (Linux) privilege"
)

# What the refusal must never degrade to.
BARE_ERRNO = ("EACCES", "EPERM", "Permission denied")


@dataclass(frozen=True)
class Tools:
    undelete: Path
    carve: Path
    imagegen: Path


class Ledger:
    """One line per check, and a count of what did not pass.

    `inconclusive` costs exactly what a failure costs. A negative test that
    cannot show the door was locked has shown nothing, and reporting that as a
    pass is how a check comes to certify its own blind spot.
    """

    def __init__(self) -> None:
        self.failures = 0

    def record(self, name: str, problems: list[str]) -> None:
        if not problems:
            print(f"PASS          {name}")
            return
        self.failures += 1
        print(f"FAIL          {name}")
        for problem in problems:
            print(f"              {problem}")

    def inconclusive(self, name: str, why: str) -> None:
        self.failures += 1
        print(f"INCONCLUSIVE  {name}\n              {why}")


def run_tool(binary: Path, *arguments: str, as_user: str = "") -> tuple[int, list[str]]:
    """One of our binaries, optionally with privilege dropped."""
    dropped = {"user": as_user, "group": "nogroup", "extra_groups": []} if as_user else {}
    finished = subprocess.run(
        [str(binary), *arguments], capture_output=True, text=True, check=False, **dropped
    )
    return finished.returncode, (finished.stdout + finished.stderr).splitlines()


def build(scratch: Path) -> Tools:
    """The three binaries, without a preset.

    Every preset pins the vcpkg toolchain the workbench does not have, and with
    the tests off the tree needs no dependency at all — `gtest` is `vcpkg.json`'s
    only entry.
    """
    build_directory = scratch / "build"
    configure = [
        "cmake", "-S", str(REPOSITORY), "-B", str(build_directory), "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DREVENANT_BUILD_TESTS=OFF",
    ]  # fmt: skip
    targets = ["revenant-carve", "revenant-undelete", "revenant-imagegen"]
    for command in (configure, ["cmake", "--build", str(build_directory), "--target", *targets]):
        if subprocess.run(command, capture_output=True, text=True, check=False).returncode != 0:
            raise SystemExit(f"ABORT         {' '.join(command[:2])} failed")
    return Tools(
        undelete=build_directory / "src/revenant-undelete",
        carve=build_directory / "src/revenant-carve",
        imagegen=build_directory / "tools/imagegen/revenant-imagegen",
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


def listing(tools: Tools, source: str | Path) -> list[str]:
    return run_tool(tools.undelete, "--source", str(source), "--list-partitions")[1]


def recover(tools: Tools, source: str | Path, destination: Path, *partition: str) -> list[str]:
    destination.mkdir(parents=True, exist_ok=True)
    return run_tool(
        tools.undelete, "--source", str(source), "--destination", str(destination), *partition
    )[1]


def carve(tools: Tools, source: str | Path, destination: Path) -> list[str]:
    destination.mkdir(parents=True, exist_ok=True)
    return run_tool(tools.carve, "--source", str(source), "--destination", str(destination))[1]


def check_listing(ledger: Ledger, tools: Tools, disk: Path, device: str) -> None:
    over_device = listing(tools, device)
    ledger.record(
        "--list-partitions over the device matches the image file",
        identity.listing_problems(listing(tools, disk), over_device, scheme="MBR", partitions=4),
    )
    ours = [int(line.split("length ")[1].split(",")[0]) for line in over_device if ": offset " in line]
    ledger.record(
        "our lengths match the kernel's own scan of the same table",
        identity.kernel_length_problems(ours, loop_device.partition_sizes(device)),
    )


def check_recovery(ledger: Ledger, tools: Tools, disk: Path, device: str, work: Path) -> None:
    destinations = [work / "recover-image", work / "recover-device"]
    for source, destination in zip((disk, device), destinations, strict=True):
        recover(tools, source, destination, "--partition", "1")
    trees = [identity.tree_digest(place, skip=".revenant") for place in destinations]
    ledger.record(
        "a --partition 1 recovery writes the same artifacts",
        identity.tree_problems(*trees, what="recovered artifacts"),
    )
    sessions = [
        identity.tree_digest(place / ".revenant", skip="manifest.json") for place in destinations
    ]
    ledger.record(
        "the session directory is identical but for the manifest",
        identity.tree_problems(*sessions, what="session files"),
    )
    ledger.record(
        "the manifest differs only where it records where it was pointed",
        identity.manifest_problems(
            *(place / ".revenant/manifest.json" for place in destinations),
            {"source": str(disk), "destination": str(destinations[0])},
            {"source": device, "destination": str(destinations[1])},
        ),
    )


def check_4kn_carve(ledger: Ledger, tools: Tools, disk: Path, device: str, work: Path) -> None:
    """The alignment arithmetic's first run at 4Kn geometry, anywhere.

    Nothing is asserted about partitions here: at a 4096-byte sector size the
    kernel re-reads the same MBR with its LBAs scaled as 4 KiB units, so its
    scan is no longer a reading of the question we are asking.
    """
    measured = loop_device.sector_size(device)
    if measured != 4096:
        ledger.inconclusive(
            "a whole-device carve at 4Kn matches the image file",
            f"the attachment reports a {measured}-byte sector; nothing ran at 4Kn",
        )
        return
    destinations = [work / "carve-image", work / "carve-4kn"]
    for source, destination in zip((disk, device), destinations, strict=True):
        carve(tools, source, destination)
    trees = [identity.tree_digest(place, skip=".revenant") for place in destinations]
    ledger.record(
        "a whole-device carve at 4Kn matches the image file",
        identity.tree_problems(*trees, what="carved artifacts"),
    )


def check_backup_header(ledger: Ledger, tools: Tools, damaged: Path, device: str) -> None:
    """An end-of-device read, addressed from what `BLKGETSIZE64` answered."""
    over_device = listing(tools, device)
    problems = identity.listing_problems(
        listing(tools, damaged), over_device, scheme="GPT", partitions=2
    )
    if not any(" (read from the backup header)" in line for line in over_device):
        problems.append(f"the listing does not say it read the backup header: {over_device}")
    ledger.record("a wiped primary GPT is listed from the backup header", problems)


def check_unprivileged(ledger: Ledger, tools: Tools, device: str, user: str) -> None:
    """The refusal M4 wrote a sentence for, produced by an actual refusal."""
    name = "an unprivileged open ends in the sentence, not a bare errno"
    probe = subprocess.run(
        ["dd", f"if={device}", "of=/dev/null", "bs=512", "count=1"],
        capture_output=True, check=False, user=user, group="nogroup", extra_groups=[],
    )  # fmt: skip
    if probe.returncode == 0:
        ledger.inconclusive(name, f"{user} can read {device}; the door was never locked")
        return
    status, output = run_tool(tools.undelete, "--source", device, "--list-partitions", as_user=user)
    text = "\n".join(output)
    problems = [] if status != 0 else ["the run exited 0"]
    if PERMISSION_SENTENCE not in text:
        problems.append(f"the sentence is not in the output verbatim: {output}")
    problems += [f"the output leaks a bare {bare}" for bare in BARE_ERRNO if bare in text]
    ledger.record(name, problems)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scratch", type=Path, default=Path("/var/tmp/revenant-loopdev"))
    parser.add_argument("--unprivileged-user", default="nobody")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if os.geteuid() != 0:
        raise SystemExit("ABORT         losetup needs root; run under `wsl.exe -d Debian -u root`")
    work = args.scratch / "work"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    args.scratch.chmod(0o755)
    tools = build(args.scratch)
    disk, damaged_gpt = fixtures(tools, work)

    ledger = Ledger()
    with loop_device.attached(disk, partition_scan=True) as device:
        print(f"# {device} <- {disk}, node {loop_device.node_mode(device)}, "
              f"{loop_device.size_bytes(device)} bytes, "
              f"{loop_device.sector_size(device)}-byte sectors")  # fmt: skip
        check_listing(ledger, tools, disk, device)
        check_recovery(ledger, tools, disk, device, work)
        check_unprivileged(ledger, tools, device, args.unprivileged_user)
    with loop_device.attached(disk, sector_size=4096) as device:
        print(f"# {device} <- {disk}, {loop_device.sector_size(device)}-byte sectors")
        check_4kn_carve(ledger, tools, disk, device, work)
    with loop_device.attached(damaged_gpt) as device:
        print(f"# {device} <- {damaged_gpt}, primary GPT header wiped")
        check_backup_header(ledger, tools, damaged_gpt, device)

    print(f"\n{ledger.failures} check(s) did not pass")
    return ledger.failures


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
