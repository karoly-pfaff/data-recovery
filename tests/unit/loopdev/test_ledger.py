#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for `tools/loopdev/ledger.py`.

The ledger exists to catch a green report that proves less than it looks like —
so it is exactly the thing that must not be trusted untested. `finish` compares
the *names* of the checks that ran against the names the pass claims to run,
because a count cannot tell a skipped check from one that ran twice, and both
print nothing but PASS lines.

Run by ctest as `LoopdevUnitTests`; `python3 -m unittest` from the repository
root works too.
"""
from __future__ import annotations

import contextlib
import io
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools" / "loopdev"))

import ledger  # noqa: E402

EVERY = ("first", "second", "third")


def run_all(book: ledger.Ledger, *, names: tuple[str, ...] = EVERY) -> str:
    """Record every named check as passing, and return what was printed."""
    printed = io.StringIO()
    with contextlib.redirect_stdout(printed):
        for name in names:
            book.record(name, [])
        book.finish()
    return printed.getvalue()


class VerdictTest(unittest.TestCase):
    def test_a_check_with_no_problems_passes_and_costs_nothing(self):
        book = ledger.Ledger(expected=EVERY)
        printed = run_all(book)
        self.assertEqual(book.failures, 0)
        self.assertIn("PASS          first", printed)

    def test_a_check_with_problems_fails_and_says_why(self):
        book = ledger.Ledger(expected=("only",))
        printed = io.StringIO()
        with contextlib.redirect_stdout(printed):
            book.record("only", ["the bytes differ"])
        self.assertEqual(book.failures, 1)
        self.assertIn("the bytes differ", printed.getvalue())

    # A negative test that cannot show the door was locked has shown nothing.
    def test_an_inconclusive_check_costs_what_a_failure_costs(self):
        book = ledger.Ledger(expected=("only",))
        with contextlib.redirect_stdout(io.StringIO()):
            book.inconclusive("only", "the door was never locked")
        self.assertEqual(book.failures, 1)


class ScopeTest(unittest.TestCase):
    def test_every_expected_check_running_once_is_the_whole_scope(self):
        book = ledger.Ledger(expected=EVERY)
        self.assertEqual(run_all(book).count("FAIL"), 0)
        self.assertEqual(book.scope_problems(), [])

    def test_a_check_that_never_ran_is_caught(self):
        book = ledger.Ledger(expected=EVERY)
        printed = run_all(book, names=("first", "second"))
        self.assertEqual(book.failures, 1)
        self.assertIn("never ran: third", printed)

    # The failure a count cannot see: as many verdicts as expected, but one of
    # them twice and another not at all.
    def test_a_check_that_ran_twice_in_place_of_another_is_caught(self):
        book = ledger.Ledger(expected=EVERY)
        printed = run_all(book, names=("first", "second", "second"))
        self.assertEqual(book.failures, 1)
        self.assertIn("ran more than once: second", printed)
        self.assertIn("never ran: third", printed)

    def test_a_verdict_nobody_expected_is_caught(self):
        book = ledger.Ledger(expected=EVERY)
        printed = run_all(book, names=(*EVERY, "fourth"))
        self.assertEqual(book.failures, 1)
        self.assertIn("was never expected: fourth", printed)

    # An INCONCLUSIVE check ran. Without its bookkeeping the scope audit would
    # also call it "never ran", counting one blind spot as two failures and
    # naming the wrong one.
    def test_an_inconclusive_check_counts_as_having_run(self):
        book = ledger.Ledger(expected=("only",))
        printed = io.StringIO()
        with contextlib.redirect_stdout(printed):
            book.inconclusive("only", "the door was never locked")
            book.finish()
        self.assertEqual(book.failures, 1)
        self.assertNotIn("never ran", printed.getvalue())

    def test_a_pass_that_recorded_nothing_at_all_is_caught(self):
        book = ledger.Ledger(expected=EVERY)
        printed = run_all(book, names=())
        self.assertEqual(book.failures, 1)
        self.assertIn("never ran: first, second, third", printed)


if __name__ == "__main__":
    unittest.main()
