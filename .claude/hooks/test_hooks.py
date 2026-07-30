# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the Claude Code hook scripts in this directory.

Run with:  python .claude/hooks/test_hooks.py

Each hook reads a tool-event JSON payload on stdin and honours
CLAUDE_PROJECT_DIR; the tests point that at a temp sandbox so they never
touch the real working tree or the real build/*/tidy-stamps caches.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HOOKS_DIR = Path(__file__).resolve().parent
REPO_ROOT = HOOKS_DIR.parent.parent


def run_hook(script, payload, project_dir):
    """Pipe a payload into a hook script and return the CompletedProcess."""
    env = dict(os.environ, CLAUDE_PROJECT_DIR=str(project_dir))
    return subprocess.run(
        [sys.executable, str(HOOKS_DIR / script)],
        input=payload if isinstance(payload, str) else json.dumps(payload),
        capture_output=True,
        text=True,
        env=env,
        timeout=60,
    )


def edit_payload(file_path):
    return {"tool_name": "Edit", "tool_input": {"file_path": str(file_path)}}


def bash_payload(command):
    return {"tool_name": "Bash", "tool_input": {"command": command}}


class SandboxTest(unittest.TestCase):
    def setUp(self):
        self.sandbox = Path(tempfile.mkdtemp(prefix="revenant-hook-test-"))

    def tearDown(self):
        shutil.rmtree(self.sandbox, ignore_errors=True)


class FormatOnEditTest(SandboxTest):
    SCRIPT = "format_on_edit.py"
    UGLY = "int   main(  ){return 0;\n}\n"

    def setUp(self):
        super().setUp()
        style = REPO_ROOT / ".clang-format"
        if style.exists():
            shutil.copy(style, self.sandbox / ".clang-format")

    def test_formats_cpp_file_inside_project(self):
        target = self.sandbox / "Ugly.cpp"
        target.write_text(self.UGLY)
        result = run_hook(self.SCRIPT, edit_payload(target), self.sandbox)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotEqual(target.read_text(), self.UGLY, "file was not reformatted")

    def test_ignores_non_cpp_file(self):
        target = self.sandbox / "notes.md"
        target.write_text(self.UGLY)
        result = run_hook(self.SCRIPT, edit_payload(target), self.sandbox)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(target.read_text(), self.UGLY)

    def test_ignores_file_outside_project(self):
        outside = Path(tempfile.mkdtemp(prefix="revenant-hook-outside-"))
        try:
            target = outside / "Foreign.cpp"
            target.write_text(self.UGLY)
            result = run_hook(self.SCRIPT, edit_payload(target), self.sandbox)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(target.read_text(), self.UGLY)
        finally:
            shutil.rmtree(outside, ignore_errors=True)

    def test_survives_malformed_payload(self):
        result = run_hook(self.SCRIPT, "not json at all", self.sandbox)
        self.assertEqual(result.returncode, 0, result.stderr)


class InvalidateTidyStampsTest(SandboxTest):
    SCRIPT = "invalidate_tidy_stamps.py"

    def setUp(self):
        super().setUp()
        self.stamps = self.sandbox / "build" / "tidy" / "tidy-stamps"
        self.stamps.mkdir(parents=True)
        (self.stamps / "src_core_Result.cpp.stamp").write_text("")

    def test_header_edit_deletes_stamps(self):
        header = self.sandbox / "include" / "Result.hpp"
        header.parent.mkdir(parents=True)
        header.write_text("#pragma once\n")
        result = run_hook(self.SCRIPT, edit_payload(header), self.sandbox)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.stamps.exists(), "tidy-stamps survived a header edit")

    def test_source_edit_keeps_stamps(self):
        source = self.sandbox / "src" / "Result.cpp"
        source.parent.mkdir(parents=True)
        source.write_text("int x;\n")
        result = run_hook(self.SCRIPT, edit_payload(source), self.sandbox)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(self.stamps.exists(), ".cpp edit must not invalidate stamps")

    def test_header_outside_project_keeps_stamps(self):
        outside = Path(tempfile.mkdtemp(prefix="revenant-hook-outside-"))
        try:
            header = outside / "Foreign.hpp"
            header.write_text("#pragma once\n")
            result = run_hook(self.SCRIPT, edit_payload(header), self.sandbox)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(self.stamps.exists())
        finally:
            shutil.rmtree(outside, ignore_errors=True)


class GuardCommitAttributionTest(SandboxTest):
    SCRIPT = "guard_commit_attribution.py"

    def assert_blocked(self, command):
        result = run_hook(self.SCRIPT, bash_payload(command), self.sandbox)
        self.assertEqual(result.returncode, 2, f"should block: {command}")
        self.assertIn("AGENTS.md", result.stderr)

    def assert_allowed(self, command):
        result = run_hook(self.SCRIPT, bash_payload(command), self.sandbox)
        self.assertEqual(result.returncode, 0, f"should allow: {command}\n{result.stderr}")

    def test_blocks_co_authored_by_claude(self):
        self.assert_blocked(
            'git commit -m "feat: x\n\nCo-Authored-By: Claude <noreply@anthropic.com>"'
        )

    def test_blocks_generated_with_footer(self):
        self.assert_blocked(
            'git commit -m "fix: y\n\nGenerated with [Claude Code](https://claude.com)"'
        )

    def test_blocks_chained_commit(self):
        self.assert_blocked(
            'cd sub && git commit -m "feat: z\n\nCo-authored-by: Claude <x@anthropic.com>"'
        )

    def test_allows_clean_commit(self):
        self.assert_allowed('git commit -m "feat(carve): add PNG chunk walker"')

    def test_allows_non_commit_command(self):
        self.assert_allowed('grep -rn "Co-Authored-By: Claude" docs/')

    def test_allows_git_log_grep(self):
        self.assert_allowed('git log --grep "Co-Authored-By: Claude"')

    def test_survives_malformed_payload(self):
        result = run_hook(self.SCRIPT, "{broken", self.sandbox)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
