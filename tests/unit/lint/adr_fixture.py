#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A throwaway repository holding one Accepted ADR, for the gate's tests.

Shared by every gate-level test module — the rule, the frozen lines, the
ranges, the environment and the faults. Not named `test_*`, so
`unittest discover` imports it rather than collecting it.

Throwaway repositories rather than this one, because the cases have to stay
stable while the real ADRs keep changing. The one exception is the historical
breach, asserted against the real `4a4221e`.
"""
from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
GATE = REPO_ROOT / "tools" / "lint" / "check_adr_immutability.py"

ADR_DIR = "docs/architecture/adr"
THE_ADR = "adr-0005-a-decision.md"

ACCEPTED = """<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# ADR-0005: A decision

- **Status:** Accepted
- **Date:** 2026-01-01

## Context

Why.

## Decision

The decision, as accepted.

## Consequences

- What follows from it.
"""


def successor(supersedes: str = "", body: str = "The new decision.") -> str:
    """A well-formed ADR-0006, optionally declaring what it supersedes.

    Well-formed matters: an Accepted ADR may not *land* malformed, so a fixture
    missing its Consequences would be refused for that rather than for the
    thing the test is about.
    """
    return (
        "# ADR-0006: The successor\n\n"
        "- **Status:** Accepted\n"
        f"{supersedes}\n"
        f"## Decision\n\n{body}\n\n"
        "## Consequences\n\n- What follows.\n"
    )


def git(repo: pathlib.Path, *args: str) -> str:
    done = subprocess.run(
        ["git", *args], cwd=repo, capture_output=True, text=True, check=True, encoding="utf-8"
    )
    return done.stdout


def run_gate(repo: pathlib.Path, diff_range: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(GATE), diff_range],
        cwd=repo,
        capture_output=True,
        text=True,
        check=False,
        encoding="utf-8",
    )


class AdrRepository:
    """A repository with one Accepted ADR committed, ready to be edited."""

    def __init__(self, root: pathlib.Path):
        self.root = root
        git(root, "init", "-q", "-b", "main")
        git(root, "config", "user.email", "gate@test")
        git(root, "config", "user.name", "Gate Test")
        self.write(THE_ADR, ACCEPTED)
        self.commit("the accepted ADR")

    def path(self, name: str) -> pathlib.Path:
        return self.root / ADR_DIR / name

    def read(self, name: str = THE_ADR) -> str:
        return self.path(name).read_text(encoding="utf-8")

    def write(self, name: str, text: str) -> None:
        target = self.path(name)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")

    def commit(self, message: str) -> None:
        git(self.root, "add", "-A")
        git(self.root, "commit", "-q", "-m", message)


class AdrGateTest(unittest.TestCase):
    """One fresh repository per test, and the edits the cases are built from."""

    def setUp(self):
        self._tmp: tempfile.TemporaryDirectory | None = None
        self.reset()

    def reset(self) -> None:
        """A fresh repository, including part-way through a table-driven test.

        Calling `setUp()` again would leak the previous `TemporaryDirectory` and
        register a second cleanup for it — which is what the loops here did
        before, once per iteration.
        """
        if self._tmp is not None:
            self._tmp.cleanup()
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.repo = AdrRepository(pathlib.Path(self._tmp.name))

    def gate(self, diff_range: str = "HEAD~1..HEAD", *, cwd: pathlib.Path | None = None):
        return run_gate(cwd or self.repo.root, diff_range)

    def substitute(self, old: str, new: str) -> None:
        current = self.repo.read()
        self.assertIn(old, current, "the fixture no longer contains what the test edits")
        self.repo.write(THE_ADR, current.replace(old, new))

    def rename_to(self, name: str) -> None:
        git(self.repo.root, "mv", f"{ADR_DIR}/{THE_ADR}", f"{ADR_DIR}/{name}")

    def edit(self, section: str, text: str) -> None:
        current = self.repo.read()
        marker = f"## {section}\n"
        head, _, tail = current.partition(marker)
        rest = tail.split("\n##", 1)
        replaced = f"{head}{marker}\n{text}\n" + ("\n##" + rest[1] if len(rest) > 1 else "")
        self.repo.write(THE_ADR, replaced)
