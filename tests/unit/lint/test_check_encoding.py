#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for the encoding gate in `tools/lint/check_encoding.py`.

The gate exists because clang rejects a byte no other compiler here minds:
an editing pass left two raw `0x97` bytes — cp1252's em dash — in a comment
during story-0609, and MSVC and GCC compiled it in silence while clang's
`-Winvalid-utf8` failed the build. That is a red CI run for a mistake a local
check catches in a second.

What belongs to the gate, and so to these tests: a file that is not UTF-8 fails
and is named with the byte and the line, a clean tree passes, and a file set
that matches nothing refuses to pass. Discovering the file set is
`source_set`'s, and so are its tests.
"""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools" / "lint"))

import check_encoding  # noqa: E402


class CheckEncodingTest(unittest.TestCase):
    def setUp(self) -> None:
        self._temp = tempfile.TemporaryDirectory()
        self.root = Path(self._temp.name)
        self.addCleanup(self._temp.cleanup)

    def write(self, name: str, data: bytes) -> Path:
        path = self.root / name
        path.write_bytes(data)
        return path

    def test_passes_a_tree_that_is_all_utf8(self) -> None:
        self.write("clean.cpp", "// a dash — and an accent é\nint main() {}\n".encode())
        self.assertEqual(check_encoding.main([str(self.root)]), 0)

    def test_fails_a_file_holding_a_cp1252_byte(self) -> None:
        # 0x97 is cp1252's em dash: the exact byte story-0609 shipped by accident.
        self.write("bad.cpp", b"// a dash \x97 here\n")
        self.assertEqual(check_encoding.main([str(self.root)]), 1)

    def test_names_the_file_the_line_and_the_byte(self) -> None:
        self.write("bad.cpp", b"// fine\n// then \x97 this\n")
        offence = check_encoding.first_offence(self.root / "bad.cpp")
        self.assertIsNotNone(offence)
        assert offence is not None
        self.assertEqual(offence.line, 2)
        self.assertEqual(offence.byte, 0x97)

    def test_reads_a_clean_file_as_no_offence(self) -> None:
        path = self.write("clean.cpp", "// plain\n".encode())
        self.assertIsNone(check_encoding.first_offence(path))

    # A UTF-8 BOM is valid UTF-8 but is not what this tree writes, and MSVC and
    # clang disagree about what it means at the top of a source file.
    def test_fails_a_file_that_starts_with_a_byte_order_mark(self) -> None:
        self.write("bom.cpp", b"\xef\xbb\xbf// text\n")
        self.assertEqual(check_encoding.main([str(self.root)]), 1)

    # The refusal every gate here shares: checking nothing must never look like
    # checking everything and finding it clean.
    def test_refuses_a_root_that_does_not_exist(self) -> None:
        self.assertEqual(check_encoding.main([str(self.root / "nowhere")]), 2)

    def test_refuses_an_empty_match(self) -> None:
        self.assertEqual(check_encoding.main([str(self.root)]), 2)


if __name__ == "__main__":
    unittest.main()
