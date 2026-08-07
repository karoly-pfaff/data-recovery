#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Every gate that walks the tree refuses a root that matched nothing.

Not "every gate currently written does" — that is what five hand-copied guards
achieved, while `check_file_length.py` still missed it despite enforcing
AGENTS.md §2's headline number. This asserts the *mechanism*: a gate that
discovers files does so through `gate_files`, which owns the refusal, so the
seventh gate inherits it rather than remembering it (story-0704).

**Detection is over the AST, not the source text.** The first version matched the
substrings `rglob(`, `.glob(` and `os.walk(`, which misses `iterdir`, `scandir`,
`listdir`, `glob.iglob` and `Path.walk`, and fires on the words appearing in a
docstring — and this repository writes long prose in gate docstrings. It also
never executed its own positive branch, so a typo in any marker would have left
it green forever. `DetectorTest` below pins the detector against synthetic
sources, because an instrument that cannot be shown to fire is not evidence.
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

# Every way this tree could discover files for itself. `source_set` is the one
# module allowed to call them; everything else must go through `gate_files`.
DISCOVERY_CALLS = frozenset(
    {"rglob", "glob", "iglob", "iterdir", "scandir", "listdir", "walk"}
)
DISCOVERY_OWNER = "source_set.py"


def lint_modules() -> list[pathlib.Path]:
    return sorted(p for p in LINT_DIR.glob("*.py") if p.name != DISCOVERY_OWNER)


def gate_scripts() -> list[pathlib.Path]:
    return sorted(LINT_DIR.glob("check_*.py"))


def discovery_calls_in(source: str) -> set[str]:
    """Which file-discovery functions this source *calls*.

    Over the AST, so prose in a docstring — "…instead of its own rglob()" — is
    not a finding, and a call spelled `os.scandir` or `Path.iterdir` is.
    """
    found: set[str] = set()
    for node in ast.walk(ast.parse(source)):
        if not isinstance(node, ast.Call):
            continue
        callee = node.func
        name = (
            callee.attr
            if isinstance(callee, ast.Attribute)
            else callee.id if isinstance(callee, ast.Name) else None
        )
        if name in DISCOVERY_CALLS:
            found.add(name)
    return found


def uses_gate_files(source: str) -> bool:
    """Whether this source reaches `gate_files`, however it was imported.

    Both spellings occur in this tree — `from source_set import gate_files` and
    `import source_set` … `source_set.gate_files(...)` — so matching only the
    first would silently drop a gate from every assertion below.
    """
    for node in ast.walk(ast.parse(source)):
        if isinstance(node, ast.Name) and node.id == "gate_files":
            return True
        if isinstance(node, ast.Attribute) and node.attr == "gate_files":
            return True
    return False


class DetectorTest(unittest.TestCase):
    """The instrument, before anything it measures.

    In the green state the tree-walking assertion below has nothing to flag, so
    its body never runs. These make the detector's positive branch execute on
    every invocation.
    """

    def test_it_finds_a_discovery_call_however_it_is_spelled(self):
        for snippet in (
            "from pathlib import Path\nx = [p for p in Path('.').rglob('*')]\n",
            "import os\nx = os.walk('.')\n",
            "import os\nx = os.listdir('.')\n",
            "import os\nx = os.scandir('.')\n",
            "from pathlib import Path\nx = list(Path('.').iterdir())\n",
            "import glob\nx = glob.iglob('*.py')\n",
            "from glob import glob\nx = glob('*.py')\n",
        ):
            with self.subTest(snippet=snippet.strip().splitlines()[-1]):
                self.assertTrue(discovery_calls_in(snippet))

    def test_prose_about_discovery_is_not_a_call(self):
        source = '"""Use gate_files instead of your own rglob() or os.walk()."""\nx = 1\n'
        self.assertEqual(discovery_calls_in(source), set())

    def test_it_sees_gate_files_through_either_import(self):
        self.assertTrue(uses_gate_files("from source_set import gate_files\nx = gate_files([], set())\n"))
        self.assertTrue(uses_gate_files("import source_set\nx = source_set.gate_files([], set())\n"))
        self.assertFalse(uses_gate_files("import source_set\nx = source_set.source_files([], set())\n"))


class GateDiscovery(unittest.TestCase):
    def test_there_are_modules_to_inspect(self):
        # The instrument's own vacuity guard: an empty glob would agree with
        # every assertion below.
        self.assertGreaterEqual(len(gate_scripts()), 5)
        self.assertGreaterEqual(len(lint_modules()), 6)

    def test_nothing_but_source_set_discovers_files_for_itself(self):
        for module in lint_modules():
            calls = discovery_calls_in(module.read_text(encoding="utf-8"))
            if not calls:
                continue
            with self.subTest(module=module.name):
                self.assertTrue(
                    uses_gate_files(module.read_text(encoding="utf-8")),
                    f"{module.name} calls {sorted(calls)} without reaching gate_files, "
                    "so it does not inherit the empty-set refusal",
                )


class EmptyRootIsRefused(unittest.TestCase):
    """Behaviour, not structure: an existing-but-empty root must exit non-zero.

    Only gates that take roots on the command line are driven. `check_coverage`
    reads a JSON export and `check_fuzz_instrumentation` an archive — neither
    takes a root, so "run it over an empty root" would fail on argument parsing
    and pass for the wrong reason. They are identified by not reaching
    `gate_files`, never by name.

    `EXTRA_ARGS` *is* a hand-written table, and the story says so rather than
    claiming otherwise: a gate with a required flag missing from it exits
    non-zero from argparse but without `empty gate` on stderr, so the assertion
    below fails loudly rather than passing spuriously.
    """

    EXTRA_ARGS = {"check_duplication.py": ["--min-tokens", "60"]}

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def driven_gates(self) -> list[pathlib.Path]:
        return [s for s in gate_scripts() if uses_gate_files(s.read_text(encoding="utf-8"))]

    def test_the_gate_that_enforces_the_line_limit_is_among_them(self):
        # Named explicitly because it is the gate this story exists for: it had
        # no guard at all, while enforcing AGENTS.md §2's headline number.
        self.assertIn("check_file_length.py", [g.name for g in self.driven_gates()])

    def test_an_existing_but_empty_root_is_refused_by_every_walking_gate(self):
        driven = self.driven_gates()
        self.assertTrue(driven, "no gate reaches gate_files; the scan proved nothing")
        for script in driven:
            with self.subTest(gate=script.name):
                outcome = subprocess.run(
                    [sys.executable, str(script), *self.EXTRA_ARGS.get(script.name, []), str(self.root)],
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
                self.assertIn(str(self.root), outcome.stderr)


if __name__ == "__main__":
    unittest.main()
