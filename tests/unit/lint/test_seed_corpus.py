#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The seed generator makes the seeds that are checked in — asserted, not promised.

`.gitignore` says only curated seeds are tracked and that they are regenerated
with `tools/fuzz/make_seed_corpus.py`. Nothing checked it, and story-0606 found
`Ext4EnumerateFuzz/volume.bin` had drifted from the generator months earlier and
survived every CI run in between: the seed carried `inode=12` for `.` and `..`
where ext4's root is inode 2, because it predated the argument that fixes it.

Regenerating it closed that instance. This closes the mechanism, by driving the
generator through its own `write` hook and comparing what it produces with what
is committed — so the next drift fails a test rather than waiting for someone to
run the script by hand.
"""
from __future__ import annotations

import pathlib
import subprocess
import sys
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools" / "fuzz"))

import make_seed_corpus  # noqa: E402

CORPUS_ROOT = REPO_ROOT / "tests" / "fuzz" / "corpus"

# The one corpus the generator deliberately does not author: story-0609's four
# inputs came out of a fuzz run, not out of anybody's hand. The generator says
# so where it stops, and this says so where it checks.
UNAUTHORED = {"MountTableFuzz"}


def generated_seeds() -> dict[tuple[str, str], bytes]:
    """Everything `main()` would write, captured instead of written."""
    produced: dict[tuple[str, str], bytes] = {}
    original = make_seed_corpus.write
    make_seed_corpus.write = lambda target, name, payload: produced.__setitem__(
        (target, name), payload
    )
    try:
        make_seed_corpus.main()
    finally:
        make_seed_corpus.write = original
    return produced


def committed_seeds() -> set[tuple[str, str]]:
    """Every tracked `.bin` under the corpus root, as (target, name).

    Asked of git rather than of the working tree: this story's own fact-check
    got that wrong once, counting campaign leftovers `.gitignore` exists to
    ignore. "Committed" is a question only `git ls-files` answers.
    """
    listed = subprocess.run(
        ["git", "ls-files", "tests/fuzz/corpus/*/*.bin"],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=True,
    ).stdout.split()
    paths = [pathlib.PurePosixPath(line) for line in listed]
    return {(p.parent.name, p.name) for p in paths if p.parent.name not in UNAUTHORED}


class SeedCorpus(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.produced = generated_seeds()

    def test_the_generator_produces_something(self) -> None:
        """The vacuity guard: an empty capture would agree with anything."""
        self.assertGreater(len(self.produced), 30)

    def test_every_generated_seed_matches_the_committed_bytes(self) -> None:
        mismatched = []
        for (target, name), payload in self.produced.items():
            path = CORPUS_ROOT / target / name
            if not path.is_file():
                mismatched.append(f"{target}/{name}: generated but not committed")
            elif path.read_bytes() != payload:
                mismatched.append(
                    f"{target}/{name}: committed {path.stat().st_size} bytes differ from"
                    f" the {len(payload)} the generator makes"
                )
        self.assertEqual(mismatched, [])

    def test_every_committed_seed_has_a_generator(self) -> None:
        """Except the ones nobody authored, which are named above and nowhere else."""
        orphans = sorted(committed_seeds() - set(self.produced))
        self.assertEqual(orphans, [])

    def test_the_unauthored_corpus_is_still_tracked(self) -> None:
        """`UNAUTHORED` excuses a directory from the check; it must not be empty,
        or the exemption would be hiding a corpus that had quietly vanished."""
        for target in UNAUTHORED:
            self.assertTrue(list((CORPUS_ROOT / target).glob("*.bin")), target)

    def test_committed_seeds_are_read_from_git(self) -> None:
        """The vacuity guard on the instrument itself: an empty `git ls-files`
        would make `test_every_committed_seed_has_a_generator` pass over nothing."""
        self.assertGreater(len(committed_seeds()), 30)


if __name__ == "__main__":
    unittest.main()
