#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for `tools/loopdev/identity.py`, which decides whether the loop
device and the image file agreed.

story-0603's pass is manual and its first run came back green on every check —
which is exactly the state in which a comparison that never compared anything is
indistinguishable from one that compared everything. These tests are how the
harness is held to saying red: every case below names the divergence it expects
to be caught, and the vacuity cases assert that an identity with nothing in it
is a failure rather than a pass.

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

LISTING = [
    "[info] partitions: MBR, 4 found",
    "[info]   1: offset 1048576, length 4194304, NTFS/exFAT",
    "[info]   2: offset 5242880, length 2097152, FAT32 (LBA)",
    "[info]   3: offset 7340032, length 2097152, NTFS/exFAT",
    "[info]   4: offset 9437184, length 524288, Linux",
]

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


class ListingIdentityTest(unittest.TestCase):
    def test_the_same_listing_over_both_sources_agrees(self):
        self.assertEqual(identity.listing_problems(LISTING, LISTING, scheme="MBR", partitions=4), [])

    def test_a_single_differing_length_is_caught(self):
        altered = [*LISTING[:1], LISTING[1].replace("4194304", "4194303"), *LISTING[2:]]
        self.assertTrue(identity.listing_problems(LISTING, altered, scheme="MBR", partitions=4))

    def test_two_empty_listings_are_not_an_identity(self):
        self.assertTrue(identity.listing_problems([], [], scheme="MBR", partitions=4))

    def test_a_scheme_the_pass_did_not_ask_for_is_caught(self):
        gpt = ["[info] partitions: GPT, 4 found", *LISTING[1:]]
        self.assertTrue(identity.listing_problems(gpt, gpt, scheme="MBR", partitions=4))

    def test_a_listing_short_of_its_partitions_is_caught(self):
        short = LISTING[:-1]
        self.assertTrue(identity.listing_problems(short, short, scheme="MBR", partitions=4))


class ListingLengthsTest(unittest.TestCase):
    def test_reads_the_lengths_a_listing_printed_in_order(self):
        self.assertEqual(identity.lengths_in(LISTING), [4194304, 2097152, 2097152, 524288])

    def test_reads_nothing_from_a_heading_alone(self):
        self.assertEqual(identity.lengths_in(LISTING[:1]), [])


class BackupHeaderTest(unittest.TestCase):
    def test_a_listing_that_says_it_read_the_backup_header_passes(self):
        said = ["[info] partitions: GPT, 2 found (read from the backup header)"]
        self.assertEqual(identity.backup_header_problems(said), [])

    def test_an_intact_gpt_listing_does_not_satisfy_it(self):
        self.assertTrue(identity.backup_header_problems(["[info] partitions: GPT, 2 found"]))

    def test_an_empty_listing_does_not_satisfy_it(self):
        self.assertTrue(identity.backup_header_problems([]))


class RefusalTest(unittest.TestCase):
    REFUSED = [f"[error] {identity.PERMISSION_SENTENCE}"]

    def test_the_sentence_and_a_nonzero_exit_is_what_is_wanted(self):
        self.assertEqual(identity.refusal_problems(1, self.REFUSED), [])

    def test_the_sentence_with_a_zero_exit_is_caught(self):
        self.assertTrue(identity.refusal_problems(0, self.REFUSED))

    def test_a_bare_errno_instead_of_the_sentence_is_caught(self):
        self.assertTrue(identity.refusal_problems(1, ["[error] EACCES"]))

    def test_a_bare_errno_alongside_the_sentence_is_still_caught(self):
        self.assertTrue(identity.refusal_problems(1, [*self.REFUSED, "Permission denied"]))

    def test_a_reworded_sentence_is_caught(self):
        reworded = identity.PERMISSION_SENTENCE.replace("refused", "declined")
        self.assertTrue(identity.refusal_problems(1, [f"[error] {reworded}"]))

    def test_no_output_at_all_is_caught(self):
        self.assertTrue(identity.refusal_problems(1, []))


class SourceUnchangedTest(unittest.TestCase):
    def test_the_same_digest_before_and_after_agrees(self):
        self.assertEqual(identity.unchanged_problems("abc", "abc", what="the source"), [])

    def test_a_changed_digest_is_caught(self):
        self.assertTrue(identity.unchanged_problems("abc", "abd", what="the source"))

    def test_nothing_digested_is_not_an_unchanged_source(self):
        self.assertTrue(identity.unchanged_problems("", "", what="the source"))


class KernelLengthTest(unittest.TestCase):
    def test_two_parsers_reading_one_table_agree(self):
        self.assertEqual(identity.kernel_length_problems([4194304, 524288], [4194304, 524288]), [])

    def test_a_disagreement_is_caught(self):
        self.assertTrue(identity.kernel_length_problems([4194304], [4194303]))

    def test_nothing_compared_is_not_agreement(self):
        self.assertTrue(identity.kernel_length_problems([], []))
        self.assertTrue(identity.kernel_length_problems([4194304], []))


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
        trees = [identity.tree_digest(self._tree(name), excluding=".revenant") for name in ("a", "b")]
        self.assertEqual(identity.tree_problems(*trees, what="artifacts"), [])

    def test_the_skipped_directory_is_not_compared(self):
        digest = identity.tree_digest(self._tree("a"), excluding=".revenant")
        self.assertEqual(sorted(digest), ["photos/one.jpg"])

    def test_one_differing_byte_is_caught(self):
        image = identity.tree_digest(self._tree("a"), excluding=".revenant")
        device = identity.tree_digest(self._tree("b", b"recovereD"), excluding=".revenant")
        self.assertTrue(identity.tree_problems(image, device, what="artifacts"))

    def test_a_file_only_one_run_wrote_is_caught(self):
        image = identity.tree_digest(self._tree("a"), excluding=".revenant")
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

    def test_a_manifest_that_was_never_written_is_caught(self):
        self.assertTrue(self._problems(self.root / "absent.json", self.root / "gone.json"))


if __name__ == "__main__":
    unittest.main()
