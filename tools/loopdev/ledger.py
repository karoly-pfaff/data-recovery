#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""How story-0603's pass records a verdict.

One line per check and a count of what did not pass — separate from the checks
themselves because how a result is reported is not what a result is.
"""
from __future__ import annotations

from collections.abc import Iterable


class Ledger:
    """`inconclusive` costs exactly what a failure costs.

    A negative test that cannot show the door was locked has shown nothing, and
    reporting that as a pass is how a check comes to certify its own blind spot.

    `expected` is the same rule turned on the ledger itself. It is the names of
    the checks, not how many: a count cannot tell a pass that skipped one check
    from a pass that ran another twice, and both print nothing but PASS lines.
    """

    def __init__(self, expected: Iterable[str]) -> None:
        self.failures = 0
        self._expected = list(expected)
        self._recorded: list[str] = []

    def _report(self, name: str, problems: list[str]) -> None:
        """The one place a verdict is printed and a failure is counted."""
        if not problems:
            print(f"PASS          {name}")
            return
        self.failures += 1
        print(f"FAIL          {name}")
        for problem in problems:
            print(f"              {problem}")

    def record(self, name: str, problems: list[str]) -> None:
        self._recorded.append(name)
        self._report(name, problems)

    def inconclusive(self, name: str, why: str) -> None:
        self._recorded.append(name)
        self.failures += 1
        print(f"INCONCLUSIVE  {name}\n              {why}")

    def scope_problems(self) -> list[str]:
        """Which checks never ran, ran twice, or were never expected."""
        recorded, expected = self._recorded, self._expected
        return [
            f"{label}: {', '.join(names)}"
            for label, names in (
                ("never ran", [n for n in expected if n not in recorded]),
                ("ran more than once", sorted({n for n in recorded if recorded.count(n) > 1})),
                ("was never expected", [n for n in recorded if n not in expected]),
            )
            if names
        ]

    def finish(self) -> None:
        """The last verdict, and the only one that is about the ledger itself.

        It does not go through `record`, because a check that counted itself
        would be asking whether it ran.
        """
        self._report("every check the pass claims to run, ran exactly once", self.scope_problems())
