#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What story-0603's pass checks, and how a verdict is recorded.

Each check drives the shipped binaries — `revenant-undelete` and
`revenant-carve`, not a test double — against a loop device, and holds the
answer against the same binaries over the same bytes as an image file.
`identity.py` decides whether two answers agree; this module decides what is
worth asking.
"""
from __future__ import annotations

import subprocess
from pathlib import Path

import identity
import loop_device

# The sentence `RunSummary.cpp` promises an operator who cannot open the source.
# Asserted verbatim: M4 wrote it, and until story-0603 nothing had ever produced
# it from an actual refusal.
PERMISSION_SENTENCE = (
    "the operating system refused to open the source: reading a whole disk or a"
    " mounted volume needs administrator (Windows) or root/disk-group (Linux) privilege"
)

# What that refusal must never degrade to.
BARE_ERRNO = ("EACCES", "EPERM", "Permission denied")


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


def _listing(undelete: Path, source: str | Path) -> list[str]:
    return run_tool(undelete, "--source", str(source), "--list-partitions")[1]


def _run_into(binary: Path, source: str | Path, destination: Path, *extra: str) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    run_tool(binary, "--source", str(source), "--destination", str(destination), *extra)


def _lengths_in(listing: list[str]) -> list[int]:
    entries = (line for line in listing if ": offset " in line)
    return [int(line.split("length ")[1].split(",")[0]) for line in entries]


def check_listing(ledger: Ledger, undelete: Path, disk: Path, device: str) -> None:
    over_device = _listing(undelete, device)
    ledger.record(
        "--list-partitions over the device matches the image file",
        identity.listing_problems(
            _listing(undelete, disk), over_device, scheme="MBR", partitions=4
        ),
    )
    ledger.record(
        "our lengths match the kernel's own scan of the same table",
        identity.kernel_length_problems(
            _lengths_in(over_device), loop_device.partition_sizes(device)
        ),
    )


def check_recovery(ledger: Ledger, undelete: Path, disk: Path, device: str, work: Path) -> None:
    places = [work / "recover-image", work / "recover-device"]
    for source, destination in zip((disk, device), places, strict=True):
        _run_into(undelete, source, destination, "--partition", "1")
    trees = [identity.tree_digest(place, skip=".revenant") for place in places]
    ledger.record(
        "a --partition 1 recovery writes the same artifacts",
        identity.tree_problems(*trees, what="recovered artifacts"),
    )
    sessions = [identity.tree_digest(p / ".revenant", skip="manifest.json") for p in places]
    ledger.record(
        "the session directory is identical but for the manifest",
        identity.tree_problems(*sessions, what="session files"),
    )
    ledger.record(
        "the manifest differs only where it records where it was pointed",
        identity.manifest_problems(
            *(place / ".revenant/manifest.json" for place in places),
            {"source": str(disk), "destination": str(places[0])},
            {"source": device, "destination": str(places[1])},
        ),
    )


def check_4kn_carve(ledger: Ledger, carve: Path, disk: Path, device: str, work: Path) -> None:
    """The alignment arithmetic's first run at 4Kn geometry, anywhere.

    Nothing is asserted about partitions here: at a 4096-byte sector size the
    kernel re-reads the same MBR with its LBAs scaled as 4 KiB units, so its
    scan is no longer a reading of the question we are asking.
    """
    name = "a whole-device carve at 4Kn matches the image file"
    measured = loop_device.sector_size(device)
    if measured != 4096:
        ledger.inconclusive(name, f"the attachment reports a {measured}-byte sector, not 4096")
        return
    places = [work / "carve-image", work / "carve-4kn"]
    for source, destination in zip((disk, device), places, strict=True):
        _run_into(carve, source, destination)
    trees = [identity.tree_digest(place, skip=".revenant") for place in places]
    ledger.record(name, identity.tree_problems(*trees, what="carved artifacts"))


def check_backup_header(ledger: Ledger, undelete: Path, damaged: Path, device: str) -> None:
    """An end-of-device read, addressed from what `BLKGETSIZE64` answered."""
    over_device = _listing(undelete, device)
    problems = identity.listing_problems(
        _listing(undelete, damaged), over_device, scheme="GPT", partitions=2
    )
    if not any(" (read from the backup header)" in line for line in over_device):
        problems.append(f"the listing does not say it read the backup header: {over_device}")
    ledger.record("a wiped primary GPT is listed from the backup header", problems)


def check_unprivileged(ledger: Ledger, undelete: Path, device: str, user: str) -> None:
    """The refusal M4 wrote a sentence for, produced by an actual refusal."""
    name = "an unprivileged open ends in the sentence, not a bare errno"
    probe = subprocess.run(
        ["dd", f"if={device}", "of=/dev/null", "bs=512", "count=1"],
        capture_output=True, check=False, user=user, group="nogroup", extra_groups=[],
    )  # fmt: skip
    if probe.returncode == 0:
        ledger.inconclusive(name, f"{user} can read {device}; the door was never locked")
        return
    status, output = run_tool(undelete, "--source", device, "--list-partitions", as_user=user)
    text = "\n".join(output)
    problems = [] if status != 0 else ["the run exited 0"]
    if PERMISSION_SENTENCE not in text:
        problems.append(f"the sentence is not in the output verbatim: {output}")
    problems += [f"the output leaks a bare {bare}" for bare in BARE_ERRNO if bare in text]
    ledger.record(name, problems)
