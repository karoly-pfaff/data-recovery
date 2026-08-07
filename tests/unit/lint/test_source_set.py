#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for `tools/lint/source_set.py`, which owns two facts every gate
that walks the tree depends on: which files it covers, and what a root that does
not exist means.

These moved here from the format gate's tests when the duplication gate became
the third importer — the answers belong to `source_set`, not to whichever gate
happened to be asked first.

Run by ctest alongside the other gate tests; `python3 -m unittest` from the
repository root works too.
"""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools" / "lint"))

import source_set  # noqa: E402


def _touch(root: Path, relative: str) -> Path:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("// fixture\n", encoding="utf-8")
    return path


class DiscoveryTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def test_finds_cpp_and_hpp_under_every_root_sorted(self):
        beta = _touch(self.root, "src/beta.cpp")
        alpha = _touch(self.root, "include/alpha.hpp")
        found = source_set.source_files([self.root / "src", self.root / "include"], source_set.CPP_SUFFIXES)
        self.assertEqual(found, sorted([alpha, beta]))

    def test_ignores_suffixes_the_naming_contract_forbids(self):
        _touch(self.root, "src/notes.md")
        _touch(self.root, "src/legacy.h")
        kept = _touch(self.root, "src/kept.hpp")
        found = source_set.source_files([self.root / "src"], source_set.CPP_SUFFIXES)
        self.assertEqual(found, [kept])

    # Python is excluded from the *C++* set and included in the one the
    # language-independent gates ask for. This replaces an assertion that said
    # `.py` is never discovered, which was true until story-0703: deleting it
    # rather than replacing it would leave no record that the exclusion is a
    # choice about which gate, not about the tree.
    def test_the_cpp_set_excludes_python(self):
        _touch(self.root, "src/build.py")
        kept = _touch(self.root, "src/kept.hpp")
        found = source_set.source_files([self.root / "src"], source_set.CPP_SUFFIXES)
        self.assertEqual(found, [kept])

    def test_the_whole_set_includes_python(self):
        script = _touch(self.root, "src/build.py")
        header = _touch(self.root, "src/kept.hpp")
        found = source_set.source_files([self.root / "src"], source_set.ALL_SUFFIXES)
        self.assertEqual(found, sorted([script, header]))

    # A typo'd root would quietly shrink every gate's coverage — a gate that
    # checks less than it claims must stop, not pass (the shard-validation
    # rule in DevTargets.cmake, applied per root).
    def test_a_missing_root_is_refused(self):
        _touch(self.root, "src/kept.cpp")
        with self.assertRaises(FileNotFoundError):
            source_set.source_files([self.root / "src", self.root / "absent"], source_set.CPP_SUFFIXES)


class GateEntryTest(unittest.TestCase):
    """What a gate's `main` sees: the files, or `None` and a named reason."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def test_a_resolvable_root_yields_its_files(self):
        kept = _touch(self.root, "src/kept.cpp")
        found = source_set.gate_files([self.root / "src"], source_set.CPP_SUFFIXES)
        self.assertEqual(found, [kept])

    def test_a_missing_root_is_reported_and_yields_nothing(self):
        with self.assertLogs(level="ERROR") as captured:
            outcome = source_set.gate_files([self.root / "absent"], source_set.CPP_SUFFIXES)
        self.assertIsNone(outcome)
        self.assertTrue(
            any("gate root does not exist" in line for line in captured.output)
        )


class GateScopeTest(unittest.TestCase):
    """Which suffix set each gate actually asks for.

    story-0703's load-bearing decision is that two of the five gates cannot
    analyse Python — `check_format` runs clang-format, `check_layering` parses
    C++ includes — so the suffix set is per gate rather than one widened
    constant. Nothing asserted it: the acceptance criterion said those two
    "demonstrably do not" cover Python, and only the call site showed it. This
    records what each gate asks for, so widening one by accident fails here.
    """

    def _suffixes_asked_for(self, module_name: str, argv: list[str]) -> list[set[str]]:
        module = __import__(module_name)
        seen: list[set[str]] = []
        original = module.gate_files

        def recording(roots, suffixes):
            seen.append(set(suffixes))
            return []

        module.gate_files = recording
        saved_argv = sys.argv
        sys.argv = [module_name, *argv]
        try:
            module.main()
        except SystemExit:
            pass
        finally:
            module.gate_files = original
            sys.argv = saved_argv
        return seen

    def test_the_cpp_only_gates_never_ask_for_python(self):
        for name, argv in (("check_format", ["src"]), ("check_layering", ["src"])):
            with self.subTest(gate=name):
                asked = self._suffixes_asked_for(name, argv)
                self.assertTrue(asked, f"{name} never resolved a file set")
                for suffixes in asked:
                    self.assertNotIn(".py", suffixes)

    def test_the_language_independent_gates_ask_for_python(self):
        for name, argv in (
            ("check_file_length", ["src"]),
            ("check_duplication", ["--min-tokens", "60", "src"]),
            ("check_encoding", ["src"]),
        ):
            with self.subTest(gate=name):
                asked = self._suffixes_asked_for(name, argv)
                self.assertTrue(asked, f"{name} never resolved a file set")
                for suffixes in asked:
                    self.assertIn(".py", suffixes)


if __name__ == "__main__":
    unittest.main()
