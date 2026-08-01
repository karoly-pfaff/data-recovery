#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What story-0603's pass asks.

Each check drives the shipped binaries — `revenant-undelete` and
`revenant-carve`, not a test double — against a loop device, and holds the
answer against the same binaries over the same bytes as an image file.
`identity.py` decides whether an answer is right; this module decides what is
worth asking, and is the half that needs root and a real device.
"""
from __future__ import annotations

import hashlib
import subprocess
from pathlib import Path

import identity
import loop_device
from ledger import Ledger

# What the fixtures hold, owned by `tools/imagegen/disk/DiskImageBuilder.hpp`
# and `tools/fuzz/make_seed_corpus.py`'s `gpt_disk()` respectively. Named
# because a bare `4` in an assertion is a fact nobody can look up.
MBR_DISK = {"scheme": "MBR", "partitions": 4}
GPT_DISK = {"scheme": "GPT", "partitions": 2}

# The geometry a 4Kn disk has and no image file does.
FOUR_KN_SECTOR = 4096

# Where a run leaves its session (`RecoveryOptions.hpp`) and its manifest
# (`Manifest.hpp`).
SESSION_DIRECTORY = ".revenant"
MANIFEST = "manifest.json"


def digest_of(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_tool(binary: Path, *arguments: str, as_user: str = "") -> tuple[int, list[str]]:
    """One of our binaries, optionally with privilege dropped."""
    dropped = {"user": as_user, "group": "nogroup", "extra_groups": []} if as_user else {}
    finished = subprocess.run(
        [str(binary), *arguments], capture_output=True, text=True, check=False, **dropped
    )
    return finished.returncode, (finished.stdout + finished.stderr).splitlines()


def _listing(undelete: Path, source: str | Path) -> tuple[int, list[str]]:
    return run_tool(undelete, "--source", str(source), "--list-partitions")


def _run_into(binary: Path, source: str | Path, destination: Path, *extra: str) -> list[str]:
    """A run, and the problems its own exit status reports."""
    destination.mkdir(parents=True, exist_ok=True)
    status, output = run_tool(
        binary, "--source", str(source), "--destination", str(destination), *extra
    )
    return [] if status == 0 else [f"{binary.name} over {source} exited {status}: {output}"]


def _both_listings(undelete: Path, image: Path, device: str) -> tuple[list[str], list[str]]:
    """The same listing from both sources, with either exit status folded in."""
    results = [_listing(undelete, source) for source in (image, device)]
    problems = [
        f"--list-partitions over {source} exited {status}"
        for (status, _), source in zip(results, (image, device), strict=True)
        if status != 0
    ]
    return problems, [lines for _, lines in results]


def check_listing(ledger: Ledger, undelete: Path, disk: Path, device: str) -> None:
    problems, (over_image, over_device) = _both_listings(undelete, disk, device)
    ledger.record(
        "--list-partitions over the device matches the image file",
        problems + identity.listing_problems(over_image, over_device, **MBR_DISK),
    )
    ledger.record(
        "our lengths match the kernel's own scan of the same table",
        identity.kernel_length_problems(
            identity.lengths_in(over_device), loop_device.partition_sizes(device)
        ),
    )


def check_recovery(ledger: Ledger, undelete: Path, disk: Path, device: str, work: Path) -> None:
    places = [work / "recover-image", work / "recover-device"]
    problems = [
        problem
        for source, destination in zip((disk, device), places, strict=True)
        for problem in _run_into(undelete, source, destination, "--partition", "1")
    ]
    trees = [identity.tree_digest(place, excluding=SESSION_DIRECTORY) for place in places]
    ledger.record(
        "a --partition 1 recovery writes the same artifacts",
        problems + identity.tree_problems(*trees, what="recovered artifacts"),
    )
    sessions = [
        identity.tree_digest(place / SESSION_DIRECTORY, excluding=MANIFEST) for place in places
    ]
    ledger.record(
        "the session directory is identical but for the manifest",
        identity.tree_problems(*sessions, what="session files"),
    )
    ledger.record(
        "the manifest differs only where it records where it was pointed",
        identity.manifest_problems(
            *(place / SESSION_DIRECTORY / MANIFEST for place in places),
            {"source": str(disk), "destination": str(places[0])},
            {"source": device, "destination": str(places[1])},
        ),
    )


def check_4kn_carve(ledger: Ledger, carve: Path, disk: Path, device: str, work: Path) -> None:
    """A whole-device carve over a 4Kn attachment.

    What this does *not* establish is that our own arithmetic ran at 4096:
    `RawDevicePosix` falls back to 512 when `BLKSSZGET` will not answer, reads
    are buffered, and the carved bytes come out the same either way. The case
    where the two disagree has no device in it and lives in
    `AlignedReadTest.RefusesA512SizedWindowOnA4KnDevice`. What is proven here is
    that a 4Kn device is readable end to end and yields the same artifacts.
    """
    name = "a whole-device carve at 4Kn matches the image file"
    measured = loop_device.sector_size(device)
    if measured != FOUR_KN_SECTOR:
        ledger.inconclusive(name, f"the attachment reports a {measured}-byte sector")
        return
    places = [work / "carve-image", work / "carve-4kn"]
    problems = [
        problem
        for source, destination in zip((disk, device), places, strict=True)
        for problem in _run_into(carve, source, destination)
    ]
    trees = [identity.tree_digest(place, excluding=SESSION_DIRECTORY) for place in places]
    ledger.record(name, problems + identity.tree_problems(*trees, what="carved artifacts"))


def check_backup_header(ledger: Ledger, undelete: Path, damaged: Path, device: str) -> None:
    """An end-of-device read, addressed from what `BLKGETSIZE64` answered."""
    problems, (over_image, over_device) = _both_listings(undelete, damaged, device)
    ledger.record(
        "a wiped primary GPT is listed from the backup header",
        problems
        + identity.listing_problems(over_image, over_device, **GPT_DISK)
        + identity.backup_header_problems(over_device),
    )


def check_read_only(ledger: Ledger, undelete: Path, disk: Path, device: str) -> None:
    """Nothing in a run so much as asks the source for write access.

    The kernel refuses writes to a `losetup -r` node, so an `open(O_RDWR)`
    anywhere under the run would fail here and nowhere else — the structural
    half of ADR-0011, which a digest cannot see because relaxing the open flags
    alone writes nothing.
    """
    problems, (over_image, over_device) = _both_listings(undelete, disk, device)
    ledger.record(
        "a read-only attachment is read end to end",
        problems + identity.listing_problems(over_image, over_device, **MBR_DISK),
    )


def check_unprivileged(ledger: Ledger, undelete: Path, device: str, user: str) -> None:
    """The refusal M4 wrote a sentence for, produced by an actual refusal."""
    name = "an unprivileged open ends in the sentence, not a bare errno"
    probe = subprocess.run(
        ["dd", f"if={device}", "of=/dev/null", "bs=512", "count=1"],
        capture_output=True,
        check=False,
        user=user,
        group="nogroup",
        extra_groups=[],
    )
    if probe.returncode == 0:
        ledger.inconclusive(name, f"{user} can read {device}; the door was never locked")
        return
    status, output = run_tool(undelete, "--source", device, "--list-partitions", as_user=user)
    ledger.record(name, identity.refusal_problems(status, output))
