#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""How story-0603's pass records a verdict.

One line per check and a count of what did not pass — separate from the checks
themselves because how a result is reported is not what a result is.
"""
from __future__ import annotations


class Ledger:
    """`inconclusive` costs exactly what a failure costs.

    A negative test that cannot show the door was locked has shown nothing, and
    reporting that as a pass is how a check comes to certify its own blind spot.

    `expected` is the same rule turned on the ledger itself: a pass that
    silently ran nine of its ten checks would otherwise print nothing but PASS
    lines and a zero.
    """

    def __init__(self, expected: int) -> None:
        self.failures = 0
        self._expected = expected
        self._ran = 0

    def record(self, name: str, problems: list[str]) -> None:
        self._ran += 1
        if not problems:
            print(f"PASS          {name}")
            return
        self.failures += 1
        print(f"FAIL          {name}")
        for problem in problems:
            print(f"              {problem}")

    def inconclusive(self, name: str, why: str) -> None:
        self._ran += 1
        self.failures += 1
        print(f"INCONCLUSIVE  {name}\n              {why}")

    def finish(self) -> None:
        """The last verdict, and the only one that is about the ledger itself.

        It does not go through `record`, because a check that counted itself
        would be asking whether it ran.
        """
        name = "every check the pass claims to run, ran"
        if self._ran == self._expected:
            print(f"PASS          {name}")
            return
        self.failures += 1
        print(f"FAIL          {name}\n              {self._ran} ran, not {self._expected}")
