#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for `tools/lint/check_citations.py`, over a fixture tree.

Over a tree this test builds rather than the real one, so the cases stay put
while `docs/` and `src/` move. The real tree is the integration case, and it is
the story's own acceptance criterion: the gate fails on it before the citations
are fixed and passes after.

Most citations in this repository are bare basenames — `JpegCarver.cpp:47`, no
directory — so resolution has *three* outcomes, not two. A gate that resolved
only the path as written would have reported a hundred honest citations as
missing files on its first run.
"""
from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
GATE = REPO_ROOT / "tools" / "lint" / "check_citations.py"


class Outcome:
    """What the gate said, decoded here rather than by `subprocess`.

    Text mode decodes on a reader thread, where a `UnicodeDecodeError` cannot be
    caught at the call and surfaces as `stderr is None` — the same trap
    story-0705 hit in `run_git`. The gate's messages carry em-dashes and a
    Windows console is not UTF-8.
    """

    def __init__(self, finished: subprocess.CompletedProcess):
        self.returncode = finished.returncode
        self.stdout = finished.stdout.decode("utf-8", "replace")
        self.stderr = finished.stderr.decode("utf-8", "replace")


def run_gate(root: pathlib.Path, *roots: str) -> Outcome:
    return Outcome(
        subprocess.run(
            [sys.executable, str(GATE), *(roots or ("docs",))],
            cwd=root, capture_output=True, check=False,
        )
    )


class CitationTree:
    """A throwaway repository: some source, some docs that cite it."""

    def __init__(self, root: pathlib.Path):
        self.root = root
        self.run("init", "-q", "-b", "main")
        self.run("config", "user.email", "gate@test")
        self.run("config", "user.name", "Gate Test")

    def run(self, *args: str) -> None:
        subprocess.run(["git", *args], cwd=self.root, check=True, capture_output=True)

    def write(self, name: str, text: str) -> None:
        target = self.root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")

    def commit(self) -> None:
        self.run("add", "-A")
        self.run("commit", "-q", "-m", "fixture")


class CitationGate(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tree = CitationTree(pathlib.Path(self._tmp.name))
        self.addCleanup(self._tmp.cleanup)
        self.tree.write("src/carve/Jpeg.cpp", "".join(f"line {n}\n" for n in range(1, 21)))

    def cite(self, citation: str) -> subprocess.CompletedProcess[str]:
        self.tree.write("docs/a-story.md", f"The interesting part is {citation}.\n")
        self.tree.commit()
        return run_gate(self.tree.root)

    def test_a_citation_inside_the_file_passes(self):
        outcome = self.cite("`src/carve/Jpeg.cpp:5-9`")
        self.assertEqual(outcome.returncode, 0, outcome.stdout + outcome.stderr)

    # The most common form after a rebase, and the one the gate exists for.
    def test_a_range_one_line_past_the_end_fails(self):
        outcome = self.cite("`src/carve/Jpeg.cpp:19-21`")
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("20 lines", outcome.stderr)
        self.assertIn("cited to 21", outcome.stderr)

    # Most citations here name no directory, so this is the ordinary case.
    def test_a_bare_basename_matching_one_file_resolves(self):
        self.assertEqual(self.cite("`Jpeg.cpp:5`").returncode, 0)

    def test_a_path_suffix_matching_one_file_resolves(self):
        self.assertEqual(self.cite("`carve/Jpeg.cpp:5`").returncode, 0)

    # Never resolved by preference: "the one under src/" works today and starts
    # citing the wrong file the day a second `Jpeg.cpp` appears.
    def test_a_basename_matching_two_files_is_ambiguous(self):
        self.tree.write("tests/Jpeg.cpp", "one\n")
        outcome = self.cite("`Jpeg.cpp:1`")
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("2 files", outcome.stderr)
        self.assertIn("tests/Jpeg.cpp", outcome.stderr)

    def test_a_basename_matching_nothing_fails(self):
        outcome = self.cite("`Absent.cpp:5`")
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("no file in the tree", outcome.stderr)

    # Both syntaxes: inline code, and a markdown link with an `#L` anchor.
    def test_a_link_anchor_is_a_citation_too(self):
        outcome = self.cite("[the walk](../src/carve/Jpeg.cpp#L19-L21)")
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("cited to 21", outcome.stderr)

    def test_a_link_anchor_inside_the_file_passes(self):
        self.assertEqual(self.cite("[the walk](../src/carve/Jpeg.cpp#L5-L9)").returncode, 0)

    # story-0704's rule: a gate that inspected nothing is not a gate that passed.
    def test_docs_holding_no_citations_at_all_is_refused(self):
        self.tree.write("docs/a-story.md", "Prose with nothing to resolve.\n")
        self.tree.commit()
        outcome = run_gate(self.tree.root)
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("empty gate", outcome.stderr)

    def test_a_root_that_holds_no_documents_is_refused(self):
        self.tree.write("docs/a-story.md", "`src/carve/Jpeg.cpp:5`\n")
        self.tree.commit()
        (self.tree.root / "empty").mkdir()
        outcome = run_gate(self.tree.root, "docs", "empty")
        self.assertEqual(outcome.returncode, 2, outcome.stdout + outcome.stderr)
        self.assertIn("empty gate", outcome.stderr)

    # There is deliberately **no** escape hatch — not a marker, not an ignore
    # comment, and not a fenced block. Every escape is a way to silence the gate,
    # and the first person under time pressure uses it on a real citation. A
    # fenced example is checked like any other, which is why writing *about* the
    # notation means naming the file and the lines in separate columns, as this
    # story's own tables do.
    def test_a_citation_inside_a_code_fence_is_still_checked(self):
        self.tree.write(
            "docs/a-story.md",
            "Like this:\n\n```markdown\n`Absent.cpp:999`\n```\n",
        )
        self.tree.commit()
        outcome = run_gate(self.tree.root)
        self.assertEqual(outcome.returncode, 1, outcome.stdout + outcome.stderr)
        self.assertIn("no file in the tree", outcome.stderr)

    def test_the_message_names_the_document_and_the_line(self):
        outcome = self.cite("`Absent.cpp:5`")
        self.assertIn("docs/a-story.md:1", outcome.stderr)


class TheRealTree(unittest.TestCase):
    """The gate over this repository's own `docs/`, which is the point of it.

    story-0706 exists because stale citations were fixed by hand four times in
    one milestone. If this fails, a citation somewhere in `docs/` has gone stale
    and the message says which.
    """

    def test_every_citation_in_this_repository_resolves(self):
        outcome = run_gate(REPO_ROOT)
        self.assertEqual(outcome.returncode, 0, outcome.stdout + outcome.stderr)


if __name__ == "__main__":
    unittest.main()
