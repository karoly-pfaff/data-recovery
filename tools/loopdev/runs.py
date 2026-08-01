#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What story-0603's pass runs, and what it gets back.

The shipped binaries — `revenant-undelete` and `revenant-carve`, not a test
double — driven once over the loop device and once over the image file holding
the same bytes. Producing the two answers is this module's whole job; deciding
whether they agree is `identity.py`'s, and saying so is `checks.py`'s.

Every run's own exit status is carried out with its output. A recovery that
exited nonzero is a problem in its own right, not something to be inferred later
from a tree that happens to differ.
"""
from __future__ import annotations

import hashlib
import subprocess
from dataclasses import dataclass, field
from pathlib import Path

# Where a run leaves its session (`RecoveryOptions.hpp`) and its manifest
# (`Manifest.hpp`).
SESSION_DIRECTORY = ".revenant"
MANIFEST = "manifest.json"


@dataclass(frozen=True)
class Pair:
    """One question asked of both sources, and what each answered."""

    problems: list[str] = field(default_factory=list)
    image: list[str] = field(default_factory=list)
    device: list[str] = field(default_factory=list)


@dataclass(frozen=True)
class Written:
    """Where a run over each source put its output."""

    problems: list[str]
    image: Path
    device: Path

    def sessions(self) -> tuple[Path, Path]:
        return self.image / SESSION_DIRECTORY, self.device / SESSION_DIRECTORY

    def manifests(self) -> tuple[Path, Path]:
        return tuple(session / MANIFEST for session in self.sessions())


def digest_of(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_tool(binary: Path, *arguments: str, as_user: str = "") -> tuple[int, list[str]]:
    """One of our binaries, optionally with privilege dropped."""
    dropped = {"user": as_user, "group": "nogroup", "extra_groups": []} if as_user else {}
    finished = subprocess.run(
        [str(binary), *arguments], capture_output=True, text=True, check=False, **dropped
    )
    return finished.returncode, (finished.stdout + finished.stderr).splitlines()


def listing(undelete: Path, source: str | Path) -> tuple[int, list[str]]:
    return run_tool(undelete, "--source", str(source), "--list-partitions")


def listings_of(undelete: Path, image: Path, device: str) -> Pair:
    """`--list-partitions` over both sources."""
    answers = [listing(undelete, source) for source in (image, device)]
    problems = [
        f"--list-partitions over {source} exited {status}"
        for (status, _), source in zip(answers, (image, device), strict=True)
        if status != 0
    ]
    return Pair(problems=problems, image=answers[0][1], device=answers[1][1])


def _into(binary: Path, source: str | Path, destination: Path, *extra: str) -> list[str]:
    destination.mkdir(parents=True, exist_ok=True)
    status, output = run_tool(
        binary, "--source", str(source), "--destination", str(destination), *extra
    )
    return [] if status == 0 else [f"{binary.name} over {source} exited {status}: {output}"]


def written_by(
    binary: Path, image: Path, device: str, places: tuple[Path, Path], *extra: str
) -> Written:
    """The same run over both sources, into two destinations."""
    problems = [
        problem
        for source, destination in zip((image, device), places, strict=True)
        for problem in _into(binary, source, destination, *extra)
    ]
    return Written(problems=problems, image=places[0], device=places[1])
