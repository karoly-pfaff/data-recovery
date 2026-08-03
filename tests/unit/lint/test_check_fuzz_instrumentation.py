#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The fuzz-instrumentation gate's own unit.

The gate it guards is invisible from outside — an uninstrumented fuzz build
runs and exits zero — so this asserts the two verdicts and, above all, that a
missing archive fails rather than passes.
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


class InstrumentedSymbols(unittest.TestCase):
    def test_counts_every_sancov_symbol(self) -> None:
        self.assertEqual(instrumented_symbols(INSTRUMENTED_NM), 3)

    def test_an_uninstrumented_archive_counts_none(self) -> None:
        self.assertEqual(instrumented_symbols(PLAIN_NM), 0)

    def test_empty_output_counts_none(self) -> None:
        self.assertEqual(instrumented_symbols(""), 0)


class Check(unittest.TestCase):
    def test_a_missing_archive_fails(self) -> None:
        """The vacuity guard: inspecting nothing is not the same as finding nothing."""
        missing = pathlib.Path(__file__).parent / "no-such-library.a"
        self.assertEqual(check([missing]), 1)

    def test_no_archives_at_all_fails(self) -> None:
        """A gate handed nothing must not report what a clean gate reports."""
        self.assertEqual(check([]), 1)

    def test_a_file_that_is_not_an_instrumented_archive_fails(self) -> None:
        """`nm` says nothing about a text file, and nothing is a failure here."""
        self.assertEqual(check([pathlib.Path(__file__)]), 1)


if __name__ == "__main__":
    unittest.main()
