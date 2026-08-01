#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for the four decisions `tools/loopdev/checks.py` keeps.

Most of what the pass decides moved to `identity.py`, which needs no root and no
device. Four did not, because each is about whether a check's *precondition* or
its *scope* held rather than about its answer — and a precondition nobody looks
at is how a check comes to certify its own blind spot. Each takes the measured
answer as a value, so all four are held here:

- the 4Kn carve, which must not report on geometry it never got;
- the read-only recovery, which would otherwise pass just as green on a writable
  attachment — the whole substance of ADR-0011's structural half;
- the unprivileged open, which proves nothing if the door was never locked;
- the digest of the sources, which says "both" and must not be able to say it
  having watched one.

Run by ctest as `LoopdevUnitTests`; `python3 -m unittest` from the repository
root works too.
"""
from __future__ import annotations

import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools" / "loopdev"))

import checks  # noqa: E402
import identity  # noqa: E402
import ledger  # noqa: E402
import runs  # noqa: E402

SOURCES = ("the MBR disk", "the damaged GPT")


class CheckTest(unittest.TestCase):
    """A ledger that expects exactly the one check under test."""

    NAME = ""

    def setUp(self):
        self.book = ledger.Ledger(expected=(self.NAME,))
        self.printed = io.StringIO()

    @contextlib.contextmanager
    def watching(self):
        with contextlib.redirect_stdout(self.printed):
            yield

    def assertInconclusive(self):
        self.assertEqual(self.book.failures, 1)
        self.assertIn("INCONCLUSIVE", self.printed.getvalue())
        self.assertEqual(self.book.scope_problems(), [], "the check must still count as run")


class WrittenMixin:
    """Two destinations holding the same artifact, which is the passing case."""

    def written(self, root: Path, contents: bytes = b"recovered") -> runs.Written:
        places = []
        for name in ("image", "device"):
            place = root / name
            place.mkdir(parents=True)
            (place / "one.jpg").write_bytes(contents)
            places.append(place)
        return runs.Written(problems=[], image=places[0], device=places[1])


class FourKnCarveTest(CheckTest, WrittenMixin):
    NAME = checks.FOUR_KN_CARVE

    def setUp(self):
        super().setUp()
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)

    def test_a_4kn_attachment_carves_the_same_artifacts(self):
        with self.watching():
            checks.check_4kn_carve(self.book, self.written(self.root), checks.FOUR_KN_SECTOR)
        self.assertEqual(self.book.failures, 0)

    # The sector size the story's criterion rests on. A 512-byte attachment
    # asked this question would answer it about the wrong geometry.
    def test_a_512_byte_attachment_is_inconclusive_not_passing(self):
        with self.watching():
            checks.check_4kn_carve(self.book, self.written(self.root), 512)
        self.assertInconclusive()


class ReadOnlyTest(CheckTest, WrittenMixin):
    NAME = checks.READ_ONLY

    def setUp(self):
        super().setUp()
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)

    def test_a_recovery_out_of_a_refusing_device_passes(self):
        with self.watching():
            checks.check_read_only(self.book, self.written(self.root), refuses_writes=True)
        self.assertEqual(self.book.failures, 0)

    # Without this branch the verdict would be identical on a writable
    # attachment, and ADR-0011's structural half would rest on nothing.
    def test_a_writable_attachment_is_inconclusive_not_passing(self):
        with self.watching():
            checks.check_read_only(self.book, self.written(self.root), refuses_writes=False)
        self.assertInconclusive()

    def test_a_refusing_device_that_recovered_something_else_fails(self):
        image = self.written(self.root / "a")
        (image.device / "one.jpg").write_bytes(b"different")
        with self.watching():
            checks.check_read_only(self.book, image, refuses_writes=True)
        self.assertEqual(self.book.failures, 1)


class SourcesUnchangedTest(CheckTest):
    NAME = checks.SOURCES_UNCHANGED
    BOTH = {
        "the MBR disk": identity.Digest(hexdigest="abc", size=10485760),
        "the damaged GPT": identity.Digest(hexdigest="def", size=32768),
    }

    def test_both_sources_unchanged_passes(self):
        with self.watching():
            checks.check_sources_unchanged(self.book, SOURCES, self.BOTH, self.BOTH)
        self.assertEqual(self.book.failures, 0)

    def test_a_source_that_changed_is_caught(self):
        after = {**self.BOTH, "the MBR disk": identity.Digest(hexdigest="abd", size=10485760)}
        with self.watching():
            checks.check_sources_unchanged(self.book, SOURCES, self.BOTH, after)
        self.assertEqual(self.book.failures, 1)

    # The verdict says "both sources". Watching one of them and reporting that
    # nothing changed is the vacuity this guard exists for, and a non-empty
    # test would not have caught it.
    def test_watching_only_one_of_the_two_sources_is_caught(self):
        one = {"the MBR disk": self.BOTH["the MBR disk"]}
        with self.watching():
            checks.check_sources_unchanged(self.book, SOURCES, one, one)
        self.assertEqual(self.book.failures, 1)
        self.assertIn("never digested: the damaged GPT", self.printed.getvalue())

    def test_watching_no_source_at_all_is_caught(self):
        with self.watching():
            checks.check_sources_unchanged(self.book, SOURCES, {}, {})
        self.assertEqual(self.book.failures, 1)

    def test_a_source_that_vanished_before_the_second_digest_is_caught(self):
        with self.watching():
            checks.check_sources_unchanged(self.book, SOURCES, self.BOTH, {})
        self.assertEqual(self.book.failures, 1)

    def test_a_source_nobody_asked_for_is_caught(self):
        extra = {**self.BOTH, "a third disk": identity.Digest(hexdigest="ghi", size=1)}
        with self.watching():
            checks.check_sources_unchanged(self.book, SOURCES, extra, extra)
        self.assertEqual(self.book.failures, 1)


class UnprivilegedTest(CheckTest):
    NAME = checks.UNPRIVILEGED
    REFUSED = (1, [f"[error] {identity.PERMISSION_SENTENCE}"])

    def check(self, attempt, *, was_refused):
        with self.watching():
            checks.check_unprivileged(
                self.book, attempt, was_refused=was_refused, user="nobody", device="/dev/loop0"
            )

    def test_a_genuine_refusal_producing_the_sentence_passes(self):
        self.check(self.REFUSED, was_refused=True)
        self.assertEqual(self.book.failures, 0)

    # The door has to be shown locked. A user who can read the node produces the
    # same refusal for a different reason, and the check must not claim it.
    def test_a_user_who_can_read_the_node_is_inconclusive_not_passing(self):
        self.check(self.REFUSED, was_refused=False)
        self.assertInconclusive()

    def test_a_bare_errno_instead_of_the_sentence_fails(self):
        self.check((1, ["[error] EACCES"]), was_refused=True)
        self.assertEqual(self.book.failures, 1)


if __name__ == "__main__":
    unittest.main()
