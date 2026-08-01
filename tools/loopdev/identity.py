#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Every pass/fail decision story-0603's pass makes.

The oracle is the image-file run: `ImageFileDevice` over these exact bytes is
the best-tested code in the tree, and the loop device serves the same bytes
through the one class that has never run. So most checks are identities, and any
divergence belongs to `RawDevice` because nothing else varies.

The decisions live here rather than beside the `subprocess` calls that produce
their inputs because they are the one part of the pass with no platform
dimension: `checks.py` needs root and a real block device, this module needs
neither, and so this is what CI can hold. Every function returns the problems it
found — an empty list means the two sides agree.

Every comparison states what it expected to find before it agrees. Two empty
listings are identical; so are two recoveries that recovered nothing, and so are
two manifests of them. An identity that never had anything to compare reports
the same green as one that compared everything.
"""
from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

# The manifest members that differ between the two runs by construction: the
# runs were pointed at different paths, and the manifest records where it was
# pointed. Every other member is compared.
PATH_MEMBERS = ("source", "destination")

# What a manifest worth comparing has in it.
REQUIRED_MEMBERS = ("mode", "winners", "suppressed", "unreadable", "artifacts")

# The note `PartitionListing.cpp` appends when a GPT answered from its backup
# copy — the end-of-device read this pass exists to provoke.
BACKUP_HEADER_NOTE = " (read from the backup header)"

# The sentence `RunSummary.cpp` promises an operator who cannot open the source,
# pinned there by `RunSummaryTest.SpellsThePrivilegeRefusalExactly` so the two
# copies cannot drift apart unnoticed.
PERMISSION_SENTENCE = (
    "the operating system refused to open the source: reading a whole disk or a"
    " mounted volume needs administrator (Windows) or root/disk-group (Linux) privilege"
)

# What that refusal must never degrade to.
BARE_ERRNO = ("EACCES", "EPERM", "Permission denied")


def lengths_in(listing: list[str]) -> list[int]:
    """The byte lengths a `--list-partitions` listing printed, in order.

    A line this cannot read is skipped rather than raised over: raising would
    report a defect in the tool under test as a harness that crashed. Both
    callers turn the shortfall into a verdict — `listing_problems` against the
    partition count it expected, `kernel_length_problems` against the kernel's
    own scan, which a short list cannot match either.
    """
    lengths = []
    for line in listing:
        # A line without the separator partitions to an empty tail, which is
        # not a number either — so one branch answers both.
        digits = line.partition("length ")[2].split(",")[0].strip()
        if digits.isdigit():
            lengths.append(int(digits))
    return lengths


def listing_problems(
    image: list[str], device: list[str], *, scheme: str, partitions: int
) -> list[str]:
    """The `--list-partitions` identity, and that it listed what it claims to."""
    problems: list[str] = []
    heading = f"partitions: {scheme}, {partitions} found"
    if not any(heading in line for line in device):
        problems.append(f"the device listing has no {heading!r} heading: {device}")
    found = len(lengths_in(device))
    if found != partitions:
        problems.append(f"the device listing has {found} entries, not {partitions}")
    if image != device:
        problems.append(f"the two listings differ:\n  image  {image}\n  device {device}")
    return problems


def kernel_length_problems(ours: list[int], kernel: list[int]) -> list[str]:
    """Our lengths against the kernel's own scan of the same table."""
    if not ours or not kernel:
        return [f"nothing to compare: ours={ours}, kernel={kernel}"]
    if ours != kernel:
        return [f"the lengths disagree:\n  ours   {ours}\n  kernel {kernel}"]
    return []


def backup_header_problems(listing: list[str]) -> list[str]:
    """That the GPT was read from the end of the device, and said so."""
    if not any(BACKUP_HEADER_NOTE in line for line in listing):
        return [f"the listing does not say it read the backup header: {listing}"]
    return []


def refusal_problems(status: int, output: list[str]) -> list[str]:
    """What an open by a user without permission must end in."""
    text = "\n".join(output)
    problems = [] if status != 0 else ["the run exited 0"]
    if PERMISSION_SENTENCE not in text:
        problems.append(f"the sentence is not in the output verbatim: {output}")
    return problems + [f"the output leaks a bare {bare}" for bare in BARE_ERRNO if bare in text]


def tree_problems(image: dict[str, str], device: dict[str, str], *, what: str) -> list[str]:
    if not image:
        return [f"the image run wrote no {what}; the identity would prove nothing"]
    names = sorted(set(image) | set(device))
    differing = [name for name in names if image.get(name) != device.get(name)]
    return [f"{what} differ: {', '.join(differing)}"] if differing else []


@dataclass(frozen=True)
class Digest:
    """A hash, and how many bytes went into it.

    The size is not decoration. `sha256` of nothing is still sixty-four
    characters, so a digest on its own cannot tell "unchanged" from "there was
    never anything here" — which is the shape of vacuous pass this whole pass
    exists to refuse.
    """

    hexdigest: str
    size: int


def unchanged_problems(before: dict[str, Digest], after: dict[str, Digest]) -> list[str]:
    """That every source the pass read came back exactly as it was (ADR-0011).

    Both emptinesses are refused, because both compare equal to themselves: a
    digest set with nothing in it, and a digest *of* nothing — `sha256` of no
    bytes is still sixty-four characters.

    What this does not police is *which* sources were watched. That is
    `Bench.sources()`, one line, read by eye; a mechanism to check a one-line
    declaration against itself is a tautology, and this story wrote three of
    them before deleting the idea.
    """
    if not before:
        return ["no source was digested, so nothing was ever watched"]
    problems: list[str] = []
    for name in sorted(set(before) | set(after)):
        was, now = before.get(name), after.get(name)
        if was is None or now is None:
            when = "only afterwards" if was is None else "only beforehand"
            problems.append(f"{name} was digested {when}")
        elif was.size == 0:
            problems.append(f"{name} held no bytes, so nothing was watched there")
        elif was != now:
            problems.append(f"{name} changed under the run: {was.hexdigest} -> {now.hexdigest}")
    return problems


def _path_problems(label: str, document: dict[str, Any], expected: dict[str, str]) -> list[str]:
    return [
        f"{label}: {member} is {document.get(member)!r}, expected {expected[member]!r}"
        for member in PATH_MEMBERS
        if document.get(member) != expected[member]
    ]


def _substance_problems(document: dict[str, Any]) -> list[str]:
    missing = [member for member in REQUIRED_MEMBERS if member not in document]
    if missing:
        return [f"the manifest is missing {', '.join(missing)}"]
    if not document["artifacts"]:
        return ["the manifest records no artifacts; the identity would prove nothing"]
    return []


def manifest_problems(
    image: Path, device: Path, expected_image: dict[str, str], expected_device: dict[str, str]
) -> list[str]:
    """The manifest identity, in the two parts the file's own contents force.

    `source` and `destination` cannot match across the two runs, and excluding
    the manifest would be the wrong repair: it is the one artifact that proves
    the run knew it was reading `/dev/loopN`. So the two paths are asserted to
    be exactly what each run was given, and the rest is compared whole.
    """
    absent = [path for path in (image, device) if not path.is_file()]
    if absent:
        return [f"no manifest was written: {', '.join(str(path) for path in absent)}"]
    try:
        documents = [json.loads(path.read_text(encoding="utf-8")) for path in (image, device)]
    except json.JSONDecodeError as broken:
        return [f"a manifest is not valid JSON: {broken}"]
    problems = _substance_problems(documents[0])
    problems += _path_problems("image run", documents[0], expected_image)
    problems += _path_problems("device run", documents[1], expected_device)
    rest = [
        {name: value for name, value in document.items() if name not in PATH_MEMBERS}
        for document in documents
    ]
    differing = sorted(
        name for name in set(rest[0]) | set(rest[1]) if rest[0].get(name) != rest[1].get(name)
    )
    return problems + [f"manifest member {name} differs between the two runs" for name in differing]
