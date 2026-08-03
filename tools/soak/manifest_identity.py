#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Whether two soak runs recovered the same files, and whether they are the right ones.

story-0606 interrupts a run over a fixture measured in hundreds of gigabytes,
resumes it, and claims the result is the one an uninterrupted run produces. Two
output trees that large cannot both be kept, so the claim is made on the
manifests: per artifact, the name it was written under, its size, its SHA-256
and the source extents it came from ([story-0115](../../docs/backlog/stories/story-0115-session-manifest.md)).
Run metadata — where the source was, how far the scan got, when it happened —
is deliberately not compared: it is what legitimately differs between a run that
stopped once and a run that never did.

The second question is the one self-consistency cannot answer: a run that
recovered nothing agrees with another run that recovered nothing. So the planted
offsets `revenant-imagegen soak` recorded are compared too, and an empty
comparison is a failure rather than a pass.
"""
from __future__ import annotations

import json
from collections.abc import Sequence
from typing import Any

# What a recovered file is, for the purpose of "the same result". `writtenName`
# is included because two runs that recover the same bytes under different names
# have not produced the same output tree.
COMPARED_FIELDS = (
    "originalName",
    "writtenName",
    "source",
    "confidence",
    "outcome",
    "bytes",
    "sha256",
    "extents",
    "invented",
)


def recovered_entries(manifest: dict[str, Any]) -> list[dict[str, Any]]:
    """The artifacts of one manifest, reduced to the compared fields and ordered."""
    entries = [
        {field: artifact.get(field) for field in COMPARED_FIELDS}
        for artifact in manifest.get("artifacts", [])
    ]
    return sorted(entries, key=lambda entry: json.dumps(entry, sort_keys=True))


def differences(control: dict[str, Any], resumed: dict[str, Any]) -> list[str]:
    """Every way the two runs' recovered files disagree; empty when identical."""
    left = recovered_entries(control)
    right = recovered_entries(resumed)
    if len(left) != len(right):
        return [f"artifact count: control {len(left)}, resumed {len(right)}"]
    found = []
    for index, (one, other) in enumerate(zip(left, right)):
        for field in COMPARED_FIELDS:
            if one[field] != other[field]:
                found.append(f"artifact {index} {field}: {one[field]!r} != {other[field]!r}")
    return found


def planted_offsets(plan_text: str) -> list[int]:
    """The offsets `revenant-imagegen soak` recorded beside the image."""
    return [int(line.split()[0]) for line in plan_text.splitlines() if line.strip()]


def unrecovered(plan_text: str, manifest: dict[str, Any]) -> list[int]:
    """Planted offsets no artifact in `manifest` claims as the start of its extent."""
    starts = {
        extent["offset"]
        for artifact in manifest.get("artifacts", [])
        for extent in artifact.get("extents", [])
    }
    return [offset for offset in planted_offsets(plan_text) if offset not in starts]


def verdict(control: dict[str, Any], resumed: dict[str, Any], plan_text: str) -> list[str]:
    """Every reason to reject the soak; empty means it passed.

    A run that recovered nothing is rejected here rather than congratulated:
    two empty manifests are identical, and identity is only worth asserting
    over a result that exists.
    """
    problems = []
    if not recovered_entries(control):
        problems.append("control run recovered nothing; there is no identity to assert")
    if not planted_offsets(plan_text):
        problems.append("the plan records no plants; the fixture proves nothing")
    problems.extend(differences(control, resumed))
    missing = unrecovered(plan_text, control)
    if missing:
        problems.append(f"{len(missing)} planted files were not recovered, first at {missing[0]}")
    return problems


def load(path: str) -> dict[str, Any]:
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def main(argv: Sequence[str]) -> int:
    if len(argv) != 4:
        raise SystemExit("usage: manifest_identity.py <control.json> <resumed.json> <image.plan>")
    with open(argv[3], encoding="utf-8") as handle:
        plan_text = handle.read()
    problems = verdict(load(argv[1]), load(argv[2]), plan_text)
    for problem in problems:
        print(f"FAIL {problem}")
    if not problems:
        control = load(argv[1])
        print(
            f"ok  {len(recovered_entries(control))} recovered files identical across the"
            f" interruption, covering all {len(planted_offsets(plan_text))} planted offsets"
        )
    return 1 if problems else 0


if __name__ == "__main__":
    import sys

    sys.exit(main(sys.argv))
