#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Finding the shipped binaries, and refusing to measure the wrong build.

The harness builds nothing. It is pointed at a build directory — or at the
directory a CI artifact was unpacked into — and drives what it finds there.

A build states what it is: CMake writes `build-info.json` beside the binaries at
configure time. Timing a debug or sanitized binary produces a figure that looks
exactly like a real one and means nothing, so a build that was not optimized is
refused rather than reported.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

# Where a binary sits, relative to the build directory: at its root once CI has
# staged an artifact, and in its own target directory in a live build tree.
_SEARCH_DIRS = (".", "src", "tools/imagegen")

_OPTIMIZED_BUILD_TYPES = ("Release", "RelWithDebInfo", "MinSizeRel")

_SUFFIX = ".exe" if sys.platform == "win32" else ""


class UnusableBuild(RuntimeError):
    """The build under measurement is missing, or is not worth measuring."""


def _build_info(build_dir: Path) -> dict:
    info = build_dir / "build-info.json"
    if not info.is_file():
        raise UnusableBuild(
            f"{info} is missing; it is written by CMake, so this is not a build directory"
        )
    return json.loads(info.read_text(encoding="utf-8"))


def require_optimized(build_dir: Path) -> str:
    """The build type, once it is one whose numbers mean something."""
    info = _build_info(build_dir)
    build_type = info.get("build_type", "")
    if build_type not in _OPTIMIZED_BUILD_TYPES:
        raise UnusableBuild(
            f"refusing to benchmark a {build_type or 'typeless'} build;"
            f" configure the `release` preset and measure that"
        )
    if info.get("sanitizers", False):
        raise UnusableBuild("refusing to benchmark a sanitized build; sanitizers distort timing")
    return build_type


def locate(build_dir: Path, name: str) -> Path:
    """The `name` executable inside `build_dir`, wherever it was put."""
    for relative in _SEARCH_DIRS:
        found = build_dir / relative / (name + _SUFFIX)
        if found.is_file():
            return found
    raise UnusableBuild(f"{name} is not in {build_dir}; build the `release` preset first")
