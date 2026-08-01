#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for the three decisions `tools/loopdev/checks.py` keeps.

Most of what the pass decides moved to `identity.py`, which needs no root and no
device. Three did not, because each is about whether a check's *precondition*
held rather than about its answer — and a precondition nobody looks at is how a
check comes to certify its own blind spot. They take plain values, so they are
held here anyway:

- the 4Kn carve, which must not report on geometry it never got;
- the read-only recovery, which would otherwise pass just as green on a writable
  attachment — the whole substance of ADR-0011's structural half;
- the digest of the sources, which must not certify that nothing changed by
  having watched nothing.

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
    DIGESTS = {"the MBR disk": identity.Digest(hexdigest="abc", size=10485760)}

    def test_the_same_digests_before_and_after_pass(self):
        with self.watching():
            checks.check_sources_unchanged(self.book, self.DIGESTS, self.DIGESTS)
        self.assertEqual(self.book.failures, 0)

    def test_a_source_that_changed_is_caught(self):
        after = {"the MBR disk": identity.Digest(hexdigest="abd", size=10485760)}
        with self.watching():
            checks.check_sources_unchanged(self.book, self.DIGESTS, after)
        self.assertEqual(self.book.failures, 1)

    # Two empty digest sets are equal, so without a guard this check would
    # certify that nothing changed having watched nothing at all.
    def test_watching_no_source_at_all_is_caught(self):
        with self.watching():
            checks.check_sources_unchanged(self.book, {}, {})
        self.assertEqual(self.book.failures, 1)
        self.assertIn("nothing was ever watched", self.printed.getvalue())

    def test_a_source_that_vanished_before_the_second_digest_is_caught(self):
        with self.watching():
            checks.check_sources_unchanged(self.book, self.DIGESTS, {})
        self.assertEqual(self.book.failures, 1)


if __name__ == "__main__":
    unittest.main()
