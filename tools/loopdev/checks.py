#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One verdict per function.

`runs.py` produces the two answers, `identity.py` decides whether they agree,
and each function here records exactly one line of the pass's report. Splitting
them that way is what lets the deciding half run in CI: these need root and a
real block device, and nothing they call does.
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


def check_listing(ledger: Ledger, listings: runs.Pair) -> None:
    ledger.record(
        "--list-partitions over the device matches the image file",
        listings.problems + identity.listing_problems(listings.image, listings.device, **MBR_DISK),
    )


def check_kernel_lengths(ledger: Ledger, listings: runs.Pair, device: str) -> None:
    """Our reading of the table against the kernel's own scan of it."""
    ledger.record(
        "our lengths match the kernel's own scan of the same table",
        identity.kernel_length_problems(
            identity.lengths_in(listings.device), loop_device.partition_sizes(device)
        ),
    )


def check_artifacts(ledger: Ledger, recovered: runs.Written) -> None:
    trees = [
        identity.tree_digest(place, excluding=runs.SESSION_DIRECTORY)
        for place in (recovered.image, recovered.device)
    ]
    ledger.record(
        "a --partition 1 recovery writes the same artifacts",
        recovered.problems + identity.tree_problems(*trees, what="recovered artifacts"),
    )


def check_session(ledger: Ledger, recovered: runs.Written) -> None:
    sessions = [
        identity.tree_digest(session, excluding=runs.MANIFEST)
        for session in recovered.sessions()
    ]
    ledger.record(
        "the session directory is identical but for the manifest",
        identity.tree_problems(*sessions, what="session files"),
    )


def check_manifest(ledger: Ledger, recovered: runs.Written, disk: Path, device: str) -> None:
    ledger.record(
        "the manifest differs only where it records where it was pointed",
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
    name = "a whole-device carve at 4Kn matches the image file"
    if measured != FOUR_KN_SECTOR:
        ledger.inconclusive(name, f"the attachment reports a {measured}-byte sector")
        return
    trees = [
        identity.tree_digest(place, excluding=runs.SESSION_DIRECTORY)
        for place in (carved.image, carved.device)
    ]
    ledger.record(name, carved.problems + identity.tree_problems(*trees, what="carved artifacts"))


def check_backup_header(ledger: Ledger, listings: runs.Pair) -> None:
    """An end-of-device read, addressed from what `BLKGETSIZE64` answered."""
    ledger.record(
        "a wiped primary GPT is listed from the backup header",
        listings.problems
        + identity.listing_problems(listings.image, listings.device, **GPT_DISK)
        + identity.backup_header_problems(listings.device),
    )


def check_read_only(ledger: Ledger, listings: runs.Pair) -> None:
    """Nothing in a run so much as asks the source for write access.

    The kernel refuses writes to a `losetup -r` node, so an `open(O_RDWR)`
    anywhere under the run would fail here and nowhere else — the structural
    half of ADR-0011, which a digest cannot see because relaxing the open flags
    alone writes nothing.
    """
    ledger.record(
        "a read-only attachment is read end to end",
        listings.problems + identity.listing_problems(listings.image, listings.device, **MBR_DISK),
    )


def check_source_unchanged(ledger: Ledger, before: str, after: str) -> None:
    ledger.record(
        "the source is byte-for-byte what it was before the pass",
        identity.unchanged_problems(before, after, what="the backing file"),
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
    name = "an unprivileged open ends in the sentence, not a bare errno"
    if not _refused(device, user):
        ledger.inconclusive(name, f"{user} can read {device}; the door was never locked")
        return
    status, output = runs.run_tool(
        undelete, "--source", device, "--list-partitions", as_user=user
    )
    ledger.record(name, identity.refusal_problems(status, output))
