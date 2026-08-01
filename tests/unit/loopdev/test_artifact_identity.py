#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for what `tools/loopdev/identity.py` says about two whole runs.

The artifact trees a recovery wrote, the session directory beside them, and the
manifest — the comparisons that carry story-0603's central claim, that the loop
device and the image file produced the same bytes. The single-answer decisions
are in `test_identity.py`.

Run by ctest as `LoopdevUnitTests`; `python3 -m unittest` from the repository
root works too.
"""
from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools" / "loopdev"))

import identity  # noqa: E402
import runs  # noqa: E402

MANIFEST = {
    "source": "/dev/loop0",
    "destination": "/var/tmp/dest",
    "mode": "hybrid",
    "winners": 5,
    "suppressed": 1,
    "unreadable": [],
    "artifacts": [{"originalName": "notes.txt", "writtenName": "notes.txt", "bytes": 74}],
}

IMAGE_PATHS = {"source": "/var/tmp/disk.img", "destination": "/var/tmp/dest-image"}
DEVICE_PATHS = {"source": "/dev/loop0", "destination": "/var/tmp/dest-device"}


class TreeIdentityTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def _tree(self, name: str, contents: bytes = b"recovered") -> Path:
        place = self.root / name
        (place / "photos").mkdir(parents=True)
        (place / "photos/one.jpg").write_bytes(contents)
        (place / ".revenant").mkdir()
        (place / ".revenant/manifest.json").write_text(name, encoding="utf-8")
        return place

    def test_the_same_artifacts_written_twice_agree(self):
        trees = [
            runs.tree_digest(self._tree(name), excluding=".revenant") for name in ("a", "b")
        ]
        self.assertEqual(identity.tree_problems(*trees, what="artifacts"), [])

    def test_the_skipped_directory_is_not_compared(self):
        digest = runs.tree_digest(self._tree("a"), excluding=".revenant")
        self.assertEqual(sorted(digest), ["photos/one.jpg"])

    def test_one_differing_byte_is_caught(self):
        image = runs.tree_digest(self._tree("a"), excluding=".revenant")
        device = runs.tree_digest(self._tree("b", b"recovereD"), excluding=".revenant")
        self.assertTrue(identity.tree_problems(image, device, what="artifacts"))

    def test_a_file_only_one_run_wrote_is_caught(self):
        image = runs.tree_digest(self._tree("a"), excluding=".revenant")
        device = dict(image)
        device.pop("photos/one.jpg")
        self.assertTrue(identity.tree_problems(image, device, what="artifacts"))

    def test_two_runs_that_wrote_nothing_are_not_an_identity(self):
        self.assertTrue(identity.tree_problems({}, {}, what="artifacts"))


class ManifestIdentityTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def _written(self, name: str, **changes: object) -> Path:
        path = self.root / f"{name}.json"
        path.write_text(json.dumps({**MANIFEST, **changes}), encoding="utf-8")
        return path

    def _problems(self, image: Path, device: Path) -> list[str]:
        return identity.manifest_problems(image, device, IMAGE_PATHS, DEVICE_PATHS)

    def _pair(self, **device_changes: object) -> list[str]:
        return self._problems(
            self._written("image", **IMAGE_PATHS),
            self._written("device", **DEVICE_PATHS, **device_changes),
        )

    def test_two_runs_differing_only_in_where_they_were_pointed_agree(self):
        self.assertEqual(self._pair(), [])

    def test_a_differing_count_is_caught(self):
        self.assertTrue(self._pair(winners=4))

    def test_a_differing_artifact_is_caught(self):
        self.assertTrue(self._pair(artifacts=[{"originalName": "notes.txt", "bytes": 75}]))

    def test_an_unreadable_range_on_one_side_only_is_caught(self):
        self.assertTrue(self._pair(unreadable=[512]))

    def test_a_manifest_that_recorded_the_wrong_source_is_caught(self):
        problems = self._problems(
            self._written("image", **IMAGE_PATHS), self._written("device", **IMAGE_PATHS)
        )
        self.assertTrue(any("device run" in problem for problem in problems))

    # The image side is checked by its own call, and would go unwitnessed if
    # only the device side were ever corrupted.
    def test_the_image_manifest_recording_the_wrong_destination_is_caught(self):
        wrong = {**IMAGE_PATHS, "destination": "/var/tmp/somewhere-else"}
        problems = self._problems(
            self._written("image", **wrong), self._written("device", **DEVICE_PATHS)
        )
        self.assertTrue(any("image run" in problem for problem in problems))

    def test_a_run_that_recovered_nothing_is_not_an_identity(self):
        image = self._written("image", **IMAGE_PATHS, artifacts=[])
        device = self._written("device", **DEVICE_PATHS, artifacts=[])
        self.assertTrue(self._problems(image, device))

    def test_a_manifest_missing_a_member_is_caught(self):
        incomplete = {name: value for name, value in MANIFEST.items() if name != "winners"}
        path = self.root / "incomplete.json"
        path.write_text(json.dumps({**incomplete, **IMAGE_PATHS}), encoding="utf-8")
        self.assertTrue(self._problems(path, self._written("device", **DEVICE_PATHS)))

    # The member-wise comparison catches a manifest that lost a member on one
    # side, so it makes the test above pass with `REQUIRED_MEMBERS` deleted.
    # This is the case only that guard catches: both runs writing the same
    # incomplete document, which is what a tool-wide regression would produce.
    def test_a_member_missing_from_both_manifests_is_caught(self):
        without = {name: value for name, value in MANIFEST.items() if name != "winners"}
        paths = []
        for name, expected in (("image", IMAGE_PATHS), ("device", DEVICE_PATHS)):
            path = self.root / f"{name}.json"
            path.write_text(json.dumps({**without, **expected}), encoding="utf-8")
            paths.append(path)
        self.assertTrue(self._problems(*paths))

    # A manifest the tool wrote as broken JSON is a failed check, not a crashed
    # harness - the policy `lengths_in` states and this had to match.
    def test_a_manifest_that_is_not_valid_json_is_caught(self):
        broken = self.root / "broken.json"
        broken.write_text('{"source": ', encoding="utf-8")
        problems = self._problems(broken, self._written("device", **DEVICE_PATHS))
        self.assertTrue(any("not valid JSON" in problem for problem in problems))

    def test_a_manifest_that_was_never_written_is_caught(self):
        self.assertTrue(self._problems(self.root / "absent.json", self.root / "gone.json"))


if __name__ == "__main__":
    unittest.main()
