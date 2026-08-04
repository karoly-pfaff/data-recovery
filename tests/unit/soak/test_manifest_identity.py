#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The soak comparator's own unit.

story-0603 learned this the hard way: a harness whose first full run passes
everything has never been seen to fail, and an identity check with nothing in it
reports exactly what a real one does. So the cases below are mostly the ways the
comparator must say no.
"""
from __future__ import annotations

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / "tools" / "soak"))

from manifest_identity import (  # noqa: E402
    differences,
    missing_fields,
    planted_offsets,
    recovered_entries,
    unrecovered,
    verdict,
)

PLAN = "0 32768\n1073741824 32768\n"


def artifact(offset: int, sha: str = "abc", name: str = "0001.jpg") -> dict:
    return {
        "originalName": "",
        "writtenName": name,
        "source": "carve",
        "confidence": "probable",
        "outcome": "complete",
        "bytes": 32768,
        "sha256": sha,
        "extents": [{"offset": offset, "length": 32768}],
        "invented": [],
        "timestamps": {"created": "2026-08-03T00:00:00Z"},
    }


def manifest(*artifacts: dict, scanned: int = 274877906944) -> dict:
    return {"scannedUpTo": scanned, "outcome": "complete", "artifacts": list(artifacts)}


class RecoveredEntries(unittest.TestCase):
    def test_drops_run_metadata_and_timestamps(self) -> None:
        entry = recovered_entries(manifest(artifact(0)))[0]
        self.assertNotIn("timestamps", entry)
        self.assertEqual(entry["sha256"], "abc")

    def test_order_of_artifacts_does_not_matter(self) -> None:
        one = manifest(artifact(0, name="a.jpg"), artifact(1073741824, name="b.jpg"))
        other = manifest(artifact(1073741824, name="b.jpg"), artifact(0, name="a.jpg"))
        self.assertEqual(differences(one, other), [])


class Differences(unittest.TestCase):
    def test_identical_runs_differ_in_nothing(self) -> None:
        self.assertEqual(differences(manifest(artifact(0)), manifest(artifact(0))), [])

    def test_a_changed_hash_is_reported(self) -> None:
        found = differences(manifest(artifact(0)), manifest(artifact(0, sha="def")))
        self.assertEqual(len(found), 1)
        self.assertIn("sha256", found[0])

    def test_a_changed_extent_is_reported(self) -> None:
        found = differences(manifest(artifact(0)), manifest(artifact(512)))
        self.assertTrue(any("extents" in line for line in found))

    def test_a_missing_artifact_is_reported(self) -> None:
        found = differences(manifest(artifact(0), artifact(1073741824)), manifest(artifact(0)))
        self.assertIn("artifact count", found[0])

    def test_run_metadata_may_differ(self) -> None:
        """The resumed run legitimately scanned in two pieces."""
        control = manifest(artifact(0), scanned=274877906944)
        resumed = manifest(artifact(0), scanned=12345)
        self.assertEqual(differences(control, resumed), [])


class GroundTruth(unittest.TestCase):
    def test_planted_offsets_are_read_from_the_plan(self) -> None:
        self.assertEqual(planted_offsets(PLAN), [0, 1073741824])

    def test_a_planted_file_nobody_recovered_is_named(self) -> None:
        self.assertEqual(unrecovered(PLAN, manifest(artifact(0))), [1073741824])

    def test_all_planted_files_recovered_leaves_nothing(self) -> None:
        both = manifest(artifact(0), artifact(1073741824))
        self.assertEqual(unrecovered(PLAN, both), [])


class Verdict(unittest.TestCase):
    def test_two_empty_manifests_are_not_a_pass(self) -> None:
        """The failure story-0603 warned about: identity over nothing."""
        problems = verdict(manifest(), manifest(), PLAN)
        self.assertTrue(any("recovered nothing" in line for line in problems))

    def test_a_plan_with_no_plants_is_not_a_pass(self) -> None:
        problems = verdict(manifest(artifact(0)), manifest(artifact(0)), "")
        self.assertTrue(any("no plants" in line for line in problems))

    def test_a_complete_matching_run_passes(self) -> None:
        both = manifest(artifact(0), artifact(1073741824, name="b.jpg"))
        self.assertEqual(verdict(both, both, PLAN), [])

    def test_a_field_neither_manifest_carries_is_not_a_pass(self) -> None:
        """A renamed manifest member reads as absent on both sides, and absent
        equals absent — the comparison would agree about nothing at all."""
        renamed = manifest(artifact(0), artifact(1073741824, name="b.jpg"))
        for entry in renamed["artifacts"]:
            entry["contentHash"] = entry.pop("sha256")
        problems = verdict(renamed, renamed, PLAN)
        self.assertTrue(any("`sha256`" in line for line in problems), problems)


class MissingFields(unittest.TestCase):
    def test_a_full_artifact_is_missing_nothing(self) -> None:
        self.assertEqual(missing_fields(manifest(artifact(0))), set())

    def test_a_dropped_field_is_named(self) -> None:
        one = manifest(artifact(0))
        del one["artifacts"][0]["extents"]
        self.assertEqual(missing_fields(one), {"extents"})

    def test_an_empty_manifest_names_nothing(self) -> None:
        """Emptiness is `verdict`'s own case; this one must not double-report it."""
        self.assertEqual(missing_fields(manifest()), set())


if __name__ == "__main__":
    unittest.main()
