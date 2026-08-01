#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for the answers `tools/loopdev/identity.py` judges one at a time.

The listing's shape and identity, the lengths in it, the backup-header note, the
refusal an unprivileged open must end in, and whether a source came back
unchanged. What two whole runs *wrote* is judged by the same module and tested
in `test_artifact_identity.py`.

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
        problems = identity.listing_problems(LISTING, LISTING, scheme="MBR", partitions=4)
        self.assertEqual(problems, [])

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

    # A listing this cannot parse must come back short rather than raise: the
    # count guard then reports it as a failed check, which is what a malformed
    # listing is. A harness that crashed would report a defect in the tool under
    # test as a defect in itself.
    def test_a_malformed_entry_is_skipped_rather_than_raised_over(self):
        mangled = ["[info]   1: offset 1048576, length , NTFS/exFAT", "[info] length ?"]
        self.assertEqual(identity.lengths_in(mangled), [])

    def test_a_malformed_entry_leaves_the_others_readable(self):
        mangled = [LISTING[1], "[info]   2: offset 5242880, length wat, FAT32", LISTING[3]]
        self.assertEqual(identity.lengths_in(mangled), [4194304, 2097152])

    # And the count guard is what turns that shortfall into a verdict.
    def test_a_listing_with_an_unreadable_entry_fails_the_identity(self):
        mangled = [*LISTING[:2], "[info]   3: offset 7340032, length ?, NTFS", *LISTING[4:]]
        self.assertTrue(identity.listing_problems(mangled, mangled, scheme="MBR", partitions=4))


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
    BEFORE = identity.Digest(hexdigest="abc", size=10485760)

    def test_the_same_digest_before_and_after_agrees(self):
        self.assertEqual(identity.unchanged_problems(self.BEFORE, self.BEFORE, what="src"), [])

    def test_a_changed_digest_is_caught(self):
        after = identity.Digest(hexdigest="abd", size=10485760)
        self.assertTrue(identity.unchanged_problems(self.BEFORE, after, what="src"))

    def test_a_source_that_changed_size_is_caught(self):
        after = identity.Digest(hexdigest="abc", size=512)
        self.assertTrue(identity.unchanged_problems(self.BEFORE, after, what="src"))

    # sha256 of nothing is still sixty-four characters, so the digests of two
    # empty files agree. Only the byte count can say nothing was ever watched.
    def test_two_empty_sources_are_not_an_unchanged_source(self):
        empty = identity.Digest(hexdigest="e3b0c442", size=0)
        self.assertTrue(identity.unchanged_problems(empty, empty, what="src"))


class KernelLengthTest(unittest.TestCase):
    def test_two_parsers_reading_one_table_agree(self):
        self.assertEqual(identity.kernel_length_problems([4194304, 524288], [4194304, 524288]), [])

    def test_a_disagreement_is_caught(self):
        self.assertTrue(identity.kernel_length_problems([4194304], [4194303]))

    def test_nothing_compared_is_not_agreement(self):
        self.assertTrue(identity.kernel_length_problems([], []))
        self.assertTrue(identity.kernel_length_problems([4194304], []))


if __name__ == "__main__":
    unittest.main()
