#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail the build when a `path:line` citation in the docs no longer resolves.

Stale citations were fixed by hand four times inside M6 — the single most
repeated review finding of the increment, and one that burns adversarial audit
rounds, which is the most expensive resource this project spends.

**What it catches:** a path naming no file, a name matching several files, and a
line range past the end of the file it names.

**What it does not catch, and this is the larger half:** a citation pointing at
the *wrong line* of a file that is long enough. That is the most common form
after a rebase, and no amount of parsing finds it. The rule that removes the
class is in `docs/code-quality.md` — cite code by symbol name, because only the
name survives a rebase. This gate stops the class regrowing; the rule stops it
being created.

**There is no escape marker**, deliberately: every escape is a way to silence
the gate, and the first person under time pressure uses it on a real citation.
To *write about* a citation, name the file and the lines in separate columns —
which costs nothing and is what `docs/backlog/stories/story-0706-*.md` does.
"""
from __future__ import annotations

import argparse
import collections
import logging
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from source_set import gate_files

GATE = "citation gate"

DOC_SUFFIXES = {".md"}

# A path with an extension, a colon, and a line or line range, inside inline
# code: `src/cli/RecoveryOptions.cpp:174-179`, or the bare `PngCarver.cpp:55-73`
# that most of this tree's citations actually use.
INLINE = re.compile(r"`([^`\s]+?\.[A-Za-z0-9]+):(\d+)(?:-(\d+))?`")

# A markdown link whose target carries a line anchor — the form CLAUDE.md asks
# for: [RecoveryOptions.cpp:42](src/cli/RecoveryOptions.cpp#L42-L51).
ANCHOR = re.compile(r"\]\((?!\w+:)([^)\s#]+?)#L(\d+)(?:-L?(\d+))?\)")


@dataclass(frozen=True)
class Citation:
    doc: str
    line: int
    raw: str
    path: str
    start: int
    end: int


@dataclass(frozen=True)
class Unresolved:
    citation: Citation
    reason: str

    def __str__(self) -> str:
        return f"{self.citation.doc}:{self.citation.line}: {self.citation.raw} — {self.reason}"


def tracked_files(repo: Path) -> list[str]:
    """Every file git knows about, which is the tree the docs cite into.

    Not a walk of the working directory: `build/`, vcpkg's installed trees and
    any stray copy would join the index and turn honest citations ambiguous.
    """
    finished = subprocess.run(
        ["git", "ls-files"], cwd=repo, capture_output=True, text=True, check=False,
        encoding="utf-8",
    )
    if finished.returncode != 0:
        raise RuntimeError(f"git ls-files failed: {finished.stderr.strip()}")
    return [line for line in finished.stdout.splitlines() if line]


def suffix_index(paths: list[str]) -> dict[str, list[str]]:
    """Every path suffix that could name each file, longest to shortest.

    `src/fs/ntfs/MftAttributes.cpp` is reachable as itself, as
    `fs/ntfs/MftAttributes.cpp`, as `ntfs/MftAttributes.cpp` and as
    `MftAttributes.cpp` — because most citations here are bare basenames, and a
    gate that resolved only the path as written would report a hundred honest
    citations as missing files.
    """
    index: dict[str, list[str]] = collections.defaultdict(list)
    for path in paths:
        parts = path.split("/")
        for start in range(len(parts)):
            index["/".join(parts[start:])].append(path)
    return index


def normalised(citation: Citation) -> str:
    """The cited path as a repo-relative one, where it plainly is one.

    A markdown link is relative to the document holding it, so `../../src/x.cpp`
    from a story file is `src/x.cpp`. Resolving that first is what keeps the
    suffix index from being asked an ambiguous question it need not answer.
    """
    if citation.path.startswith(("./", "../")):
        base = Path(citation.doc).parent
        return (base / citation.path).resolve().relative_to(Path.cwd().resolve()).as_posix()
    return citation.path


def citations_in(doc: str, text: str) -> list[Citation]:
    found: list[Citation] = []
    for number, line in enumerate(text.splitlines(), start=1):
        for pattern in (INLINE, ANCHOR):
            for match in pattern.finditer(line):
                start = int(match.group(2))
                found.append(
                    Citation(
                        doc=doc,
                        line=number,
                        raw=match.group(0),
                        path=match.group(1),
                        start=start,
                        end=int(match.group(3)) if match.group(3) else start,
                    )
                )
    return found


def resolve(citation: Citation, index: dict[str, list[str]], repo: Path) -> Unresolved | None:
    """Nothing to say when the citation resolves and the lines are in range."""
    try:
        wanted = normalised(citation)
    except ValueError:
        return Unresolved(citation, "the relative path leaves the repository")

    candidates = index.get(wanted, [])
    if not candidates:
        return Unresolved(citation, f"no file in the tree is named {wanted}")
    if len(candidates) > 1:
        listed = ", ".join(sorted(candidates))
        # Never resolved by preference: "the one under src/" works today and
        # silently cites the wrong file the day a second BootSector.cpp appears.
        return Unresolved(citation, f"names {len(candidates)} files — {listed}")

    resolved = candidates[0]
    length = len(
        (repo / resolved).read_text(encoding="utf-8", errors="replace").splitlines()
    )
    if citation.end > length:
        return Unresolved(citation, f"{resolved} is {length} lines, cited to {citation.end}")
    return None


def run_gate(docs: list[Path], repo: Path) -> tuple[int, list[Unresolved], int]:
    index = suffix_index(tracked_files(repo))
    unresolved: list[Unresolved] = []
    seen = 0
    for doc in docs:
        relative = doc.as_posix()
        for citation in citations_in(relative, doc.read_text(encoding="utf-8")):
            seen += 1
            fault = resolve(citation, index, repo)
            if fault:
                unresolved.append(fault)
    if seen == 0:
        # The second vacuity level, and the one this gate can reach on its own:
        # the roots held files, the files held no citations. A gate reporting a
        # clean pass over nothing is indistinguishable from one that looked.
        return 2, [], 0
    return (1 if unresolved else 0), unresolved, seen


def main() -> int:
    parser = argparse.ArgumentParser(description="Resolve every path:line citation.")
    parser.add_argument("roots", nargs="+")
    parser.add_argument("--repo", default=".", help="the tree the citations resolve against")
    args = parser.parse_args()
    logging.basicConfig(format="%(message)s", stream=sys.stderr)

    docs = gate_files(args.roots, DOC_SUFFIXES, GATE)
    if docs is None:
        return 2

    try:
        code, unresolved, seen = run_gate(docs, Path(args.repo))
    except RuntimeError as fault:
        logging.error("%s: %s", GATE, fault)
        return 2

    if seen == 0:
        logging.error(
            "%s: %d document(s) held no citations at all; refusing to pass an empty gate",
            GATE, len(docs),
        )
        return 2
    for fault in unresolved:
        logging.error("%s: %s", GATE, fault)
    if code == 0:
        print(f"{GATE}: {seen} citation(s) in {len(docs)} document(s) all resolve")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
