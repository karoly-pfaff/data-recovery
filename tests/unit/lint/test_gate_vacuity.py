#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Every gate that walks the tree refuses a root that matched nothing.

Not "every gate currently written does" — that is what five hand-copied guards
already achieved, and `check_file_length.py` still missed while enforcing
AGENTS.md §2's headline number. This asserts the *mechanism*: a gate that
discovers files does so through `gate_files`, which owns the refusal, so the
seventh gate inherits it rather than remembering it (story-0704).

The gate list is derived by inspecting imports. A hand-maintained list of names
is a thing that rots, and the next reviewer cannot tell an intentional exemption
from a forgotten one.
"""
from __future__ import annotations

import ast
import pathlib
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
LINT_DIR = REPO_ROOT / "tools" / "lint"

sys.path.insert(0, str(LINT_DIR))


def gate_scripts() -> list[pathlib.Path]:
    return sorted(LINT_DIR.glob("check_*.py"))


def imported_names(script: pathlib.Path) -> set[str]:
    tree = ast.parse(script.read_text(encoding="utf-8"))
    names: set[str] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.ImportFrom) and node.module == "source_set":
            names.update(alias.name for alias in node.names)
    return names


def walks_the_tree(script: pathlib.Path) -> bool:
    """Whether this gate discovers files for itself.

    Asked of the syntax rather than of a name list: a gate that calls `rglob`,
    `glob` or `walk` is discovering files, and if it does so without
    `gate_files` it has escaped the shared refusal.
    """
    source = script.read_text(encoding="utf-8")
    return any(marker in source for marker in ("rglob(", ".glob(", "os.walk("))


class GateDiscovery(unittest.TestCase):
    def test_there_are_gates_to_inspect(self):
        # The instrument's own vacuity guard: an empty glob would agree with
        # every assertion below.
        self.assertGreaterEqual(len(gate_scripts()), 5)

    def test_every_gate_that_walks_the_tree_uses_the_shared_discovery(self):
        for script in gate_scripts():
            if not walks_the_tree(script):
                continue
            with self.subTest(gate=script.name):
                self.fail(
                    f"{script.name} discovers files itself instead of through "
                    "source_set.gate_files, so it does not inherit the empty-set refusal"
                )

    def test_every_importer_of_gate_files_gets_the_refusal(self):
        importers = [s for s in gate_scripts() if "gate_files" in imported_names(s)]
        self.assertTrue(importers, "no gate imports gate_files; the scan proved nothing")
        for script in importers:
            with self.subTest(gate=script.name):
                self.assertIn("gate_files", script.read_text(encoding="utf-8"))


class EmptyRootIsRefused(unittest.TestCase):
    """Behaviour, not structure: an existing-but-empty root must exit non-zero.

    Only the gates that take roots on the command line are driven here.
    `check_coverage.py` reads a JSON export and `check_fuzz_instrumentation.py`
    an archive — neither takes a root, so running them "over an empty root"
    would fail on argument parsing and pass for the wrong reason. They are
    identified by not importing `gate_files`, never by name.
    """

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def argv_for(self, name: str) -> list[str]:
        if name == "check_file_length.py":
            return ["--warn", "200", "--max", "250", str(self.root)]
        if name == "check_duplication.py":
            return ["--min-tokens", "60", str(self.root)]
        return [str(self.root)]

    def test_an_existing_but_empty_root_is_refused_by_every_walking_gate(self):
        importers = [s for s in gate_scripts() if "gate_files" in imported_names(s)]
        self.assertTrue(importers, "no gate imports gate_files; the scan proved nothing")
        for script in importers:
            with self.subTest(gate=script.name):
                outcome = subprocess.run(
                    [sys.executable, str(script), *self.argv_for(script.name)],
                    capture_output=True,
                    text=True,
                    check=False,
                    cwd=REPO_ROOT,
                )
                self.assertNotEqual(
                    outcome.returncode,
                    0,
                    f"{script.name} passed over an empty root:\n{outcome.stdout}{outcome.stderr}",
                )
                self.assertIn("empty gate", outcome.stderr)


if __name__ == "__main__":
    unittest.main()
