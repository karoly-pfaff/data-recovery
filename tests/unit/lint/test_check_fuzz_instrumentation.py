#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The fuzz-instrumentation gate's own unit.

The gate it guards is invisible from outside — an uninstrumented fuzz build
runs and exits zero — so this asserts both verdicts, not only the failing one:
a gate whose passing branch never runs in a test would be satisfied by
`def check(...): return 1`, which fails every build including the good ones.

The symbol reader is injected rather than shelled out to, so these run wherever
Python does. `nm` is a Linux-only toolchain detail of the *gate*, not of its
verdict, and `LintUnitTests` runs on Windows too.
"""
from __future__ import annotations

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / "tools" / "lint"))

from check_fuzz_instrumentation import check, instrumented_symbols  # noqa: E402

# `nm` on an instrumented object; the three symbol kinds SanitizerCoverage emits.
INSTRUMENTED_NM = """\
0000000000000000 b __sancov_gen_
0000000000000008 b __sancov_gen_.1
0000000000000000 d __sancov_pcs
0000000000000010 T _ZN8revenant5carve10JpegCarver5carveERNS_10ByteReaderE
"""

PLAIN_NM = """\
0000000000000010 T _ZN8revenant5carve10JpegCarver5carveERNS_10ByteReaderE
0000000000000040 T _ZN8revenant10ByteReader5bytesEmm
"""


def reader(output: str):
    """A stand-in for `nm`, so the verdict is testable without a toolchain."""
    return lambda _archive: output


class InstrumentedSymbols(unittest.TestCase):
    def test_counts_every_sancov_symbol(self) -> None:
        self.assertEqual(instrumented_symbols(INSTRUMENTED_NM), 3)

    def test_an_uninstrumented_archive_counts_none(self) -> None:
        self.assertEqual(instrumented_symbols(PLAIN_NM), 0)

    def test_empty_output_counts_none(self) -> None:
        self.assertEqual(instrumented_symbols(""), 0)


class Check(unittest.TestCase):
    """This file exists — any real path will do; what it holds is the reader's job."""

    HERE = pathlib.Path(__file__)

    def test_an_instrumented_archive_passes(self) -> None:
        self.assertEqual(check([self.HERE], reader(INSTRUMENTED_NM)), 0)

    def test_an_uninstrumented_archive_fails(self) -> None:
        self.assertEqual(check([self.HERE], reader(PLAIN_NM)), 1)

    def test_one_bad_archive_among_good_ones_fails(self) -> None:
        outputs = iter([INSTRUMENTED_NM, PLAIN_NM, INSTRUMENTED_NM])
        self.assertEqual(check([self.HERE] * 3, lambda _a: next(outputs)), 1)

    def test_a_missing_archive_fails(self) -> None:
        """The vacuity guard: inspecting nothing is not the same as finding nothing."""
        missing = self.HERE.parent / "no-such-library.a"
        self.assertEqual(check([missing], reader(INSTRUMENTED_NM)), 1)

    def test_no_archives_at_all_fails(self) -> None:
        """A gate handed nothing must not report what a clean gate reports."""
        self.assertEqual(check([], reader(INSTRUMENTED_NM)), 1)


if __name__ == "__main__":
    unittest.main()
