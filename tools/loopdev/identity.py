#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Whether two runs over the same bytes agree.

The oracle of story-0603's pass is the image-file run: `ImageFileDevice` over
these exact bytes is the best-tested code in the tree, and the loop device
serves the same bytes through the one class that has never run. So every
positive check is an identity, and any divergence belongs to `RawDevice`
because nothing else varies.

Every comparison here states what it expected to find before it agrees that the
two sides match. Two empty listings are identical; so are two recoveries that
recovered nothing, and so are two manifests of them. An identity that never had
anything to compare reports the same green as one that compared everything.

Each function returns the problems it found — an empty list means the two
agree.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

# The manifest members that differ between the two runs by construction: the
# runs were pointed at different paths, and the manifest records where it was
# pointed. Every other member is compared.
PATH_MEMBERS = ("source", "destination")

# What a manifest worth comparing has in it.
REQUIRED_MEMBERS = ("mode", "winners", "suppressed", "unreadable", "artifacts")


def listing_problems(
    image: list[str], device: list[str], *, scheme: str, partitions: int
) -> list[str]:
    """The `--list-partitions` identity, and that it listed what it claims to."""
    problems: list[str] = []
    heading = f"partitions: {scheme}, {partitions} found"
    if not any(heading in line for line in device):
        problems.append(f"the device listing has no {heading!r} heading: {device}")
    entries = [line for line in device if ": offset " in line]
    if len(entries) != partitions:
        problems.append(f"the device listing has {len(entries)} partition lines, not {partitions}")
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


def tree_digest(root: Path, *, skip: str = "") -> dict[str, str]:
    """Every file under `root` by destination-relative path, hashed."""
    digests: dict[str, str] = {}
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root)
        if not path.is_file() or (skip and skip in relative.parts):
            continue
        digests[relative.as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    return digests


def tree_problems(image: dict[str, str], device: dict[str, str], *, what: str) -> list[str]:
    if not image:
        return [f"the image run wrote no {what}; the identity would prove nothing"]
    names = sorted(set(image) | set(device))
    differing = [name for name in names if image.get(name) != device.get(name)]
    return [f"{what} differ: {', '.join(differing)}"] if differing else []


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
    documents = [json.loads(path.read_text(encoding="utf-8")) for path in (image, device)]
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
