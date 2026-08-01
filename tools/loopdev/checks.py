#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One verdict per function.

`runs.py` produces the two answers, `identity.py` decides whether they agree,
and each function here records exactly one line of the pass's report. Splitting
them that way is what lets the deciding half run in CI: these need root and a
real block device, and nothing they call does.

The names are constants because the ledger holds the pass to running each of
them exactly once, and a name spelt twice would be two facts.
"""
from __future__ import annotations

import subprocess
from pathlib import Path

import identity
import loop_device
import runs
from ledger import Ledger

# What the fixtures hold, owned by `tools/imagegen/disk/DiskImageBuilder.hpp`
# and `tools/fuzz/make_seed_corpus.py`'s `gpt_disk()` respectively. Named
# because a bare `4` in an assertion is a fact nobody can look up.
MBR_DISK = {"scheme": "MBR", "partitions": 4}
GPT_DISK = {"scheme": "GPT", "partitions": 2}

# The geometry a 4Kn disk has and no image file does.
FOUR_KN_SECTOR = 4096

LISTING = "--list-partitions over the device matches the image file"
KERNEL_LENGTHS = "our lengths match the kernel's own scan of the same table"
ARTIFACTS = "a --partition 1 recovery writes the same artifacts"
SESSION = "the session directory is identical but for the manifest"
MANIFEST_FIELDS = "the manifest differs only where it records where it was pointed"
UNPRIVILEGED = "an unprivileged open ends in the sentence, not a bare errno"
FOUR_KN_CARVE = "a whole-device carve at 4Kn matches the image file"
READ_ONLY = "a read-only attachment recovers the same artifacts"
BACKUP_HEADER = "a wiped primary GPT is listed from the backup header"
SOURCES_UNCHANGED = "both sources are byte-for-byte what they were before the pass"

EVERY_CHECK = (
    LISTING,
    KERNEL_LENGTHS,
    ARTIFACTS,
    SESSION,
    MANIFEST_FIELDS,
    UNPRIVILEGED,
    FOUR_KN_CARVE,
    READ_ONLY,
    BACKUP_HEADER,
    SOURCES_UNCHANGED,
)


def _artifact_problems(written: runs.Written, what: str) -> list[str]:
    trees = [
        identity.tree_digest(place, excluding=runs.SESSION_DIRECTORY)
        for place in (written.image, written.device)
    ]
    return written.problems + identity.tree_problems(*trees, what=what)


def check_listing(ledger: Ledger, listings: runs.Pair) -> None:
    ledger.record(
        LISTING,
        listings.problems + identity.listing_problems(listings.image, listings.device, **MBR_DISK),
    )


def check_kernel_lengths(ledger: Ledger, listings: runs.Pair, device: str) -> None:
    """Our reading of the table against the kernel's own scan of it."""
    ledger.record(
        KERNEL_LENGTHS,
        identity.kernel_length_problems(
            identity.lengths_in(listings.device), loop_device.partition_sizes(device)
        ),
    )


def check_artifacts(ledger: Ledger, recovered: runs.Written) -> None:
    ledger.record(ARTIFACTS, _artifact_problems(recovered, "recovered artifacts"))


def check_session(ledger: Ledger, recovered: runs.Written) -> None:
    sessions = [
        identity.tree_digest(session, excluding=runs.MANIFEST)
        for session in recovered.sessions()
    ]
    ledger.record(SESSION, identity.tree_problems(*sessions, what="session files"))


def check_manifest(ledger: Ledger, recovered: runs.Written, disk: Path, device: str) -> None:
    ledger.record(
        MANIFEST_FIELDS,
        identity.manifest_problems(
            *recovered.manifests(),
            {"source": str(disk), "destination": str(recovered.image)},
            {"source": device, "destination": str(recovered.device)},
        ),
    )


def check_4kn_carve(ledger: Ledger, carved: runs.Written, measured: int) -> None:
    """A whole-device carve over a 4Kn attachment.

    What this does *not* establish is that our own arithmetic ran at 4096:
    `RawDevicePosix` falls back to 512 when `BLKSSZGET` will not answer, reads
    are buffered, and the carved bytes come out the same either way. The case
    where the two disagree has no device in it and lives in
    `AlignedReadTest.RefusesA512SizedWindowOnA4KnDevice`. What is proven here is
    that a 4Kn device is readable end to end and yields the same artifacts.
    """
    if measured != FOUR_KN_SECTOR:
        ledger.inconclusive(FOUR_KN_CARVE, f"the attachment reports a {measured}-byte sector")
        return
    ledger.record(FOUR_KN_CARVE, _artifact_problems(carved, "carved artifacts"))


def check_backup_header(ledger: Ledger, listings: runs.Pair) -> None:
    """An end-of-device read, addressed from what `BLKGETSIZE64` answered."""
    ledger.record(
        BACKUP_HEADER,
        listings.problems
        + identity.listing_problems(listings.image, listings.device, **GPT_DISK)
        + identity.backup_header_problems(listings.device),
    )


def check_read_only(ledger: Ledger, recovered: runs.Written, refuses_writes: bool) -> None:
    """A whole recovery out of a device the kernel will not let anyone write.

    An `open(O_RDWR)` fails on such a node, so a run that completes here never
    asked for one — the structural half of ADR-0011, which a digest cannot see
    because relaxing the open flags alone writes nothing.

    The attachment proves its own precondition first. `read_only=True` is an
    argument, and a check resting on it that never looked would pass just as
    green on a writable device.
    """
    if not refuses_writes:
        ledger.inconclusive(READ_ONLY, "the attachment is writable; nothing was refused")
        return
    ledger.record(READ_ONLY, _artifact_problems(recovered, "artifacts recovered read-only"))


def check_sources_unchanged(ledger: Ledger, before: dict, after: dict) -> None:
    """Every backing file the pass attached, digested either side of it."""
    ledger.record(
        SOURCES_UNCHANGED,
        [
            problem
            for name, digest in before.items()
            for problem in identity.unchanged_problems(digest, after[name], what=name)
        ],
    )


def _refused(device: str, user: str) -> bool:
    """That the user really cannot read the node, before anything is asked of it."""
    probe = subprocess.run(
        ["dd", f"if={device}", "of=/dev/null", "bs=512", "count=1"],
        capture_output=True,
        check=False,
        user=user,
        group="nogroup",
        extra_groups=[],
    )
    return probe.returncode != 0


def check_unprivileged(ledger: Ledger, undelete: Path, device: str, user: str) -> None:
    """The refusal M4 wrote a sentence for, produced by an actual refusal."""
    if not _refused(device, user):
        ledger.inconclusive(UNPRIVILEGED, f"{user} can read {device}; the door was never locked")
        return
    status, output = runs.run_tool(undelete, "--source", device, "--list-partitions", as_user=user)
    ledger.record(UNPRIVILEGED, identity.refusal_problems(status, output))
