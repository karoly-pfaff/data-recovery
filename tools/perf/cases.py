#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What the suite measures, and what each case counts as work.

Every case drives a shipped binary over a fixture on disk, in `--dry-run`: the
whole engine runs — the filesystem walk and the carve scan — and stops before
writing recovered files, because writing them measures the *destination's* disk
and swamps the thing under test.

Fixtures carry the work. A case that finishes near the clock's own noise is
fixed by giving it more to do, not by running it many times inside one timing,
so the sizes below are the knob and there is no inner repeat loop.
"""
from __future__ import annotations

from dataclasses import dataclass

_MIB = 1 << 20

# Big enough that process startup is a few percent of the run rather than a
# quarter of it, small enough that valgrind can walk the same fixture in under
# a minute. Both are measurements; see docs/performance/benchmarks.md.
SCAN_IMAGE_BYTES = 128 * _MIB
CARVE_CORPUS_BYTES = 8 * _MIB

# The fixed fixture volume holds seven files, which a walk gets through faster
# than a process starts; a rate measured over that would be a measurement of
# process startup. The same volume with a bigger `$MFT` is a real walk.
NTFS_MFT_RECORDS = 8192


@dataclass(frozen=True)
class Fixture:
    """An image `revenant-imagegen` makes, and the arguments that make it."""

    filename: str
    verb: str
    args: tuple[str, ...] = ()


@dataclass(frozen=True)
class Case:
    """One benchmark: a fixture, a binary to point at it, and a work unit."""

    name: str
    unit: str
    fixture: Fixture
    binary: str
    flags: tuple[str, ...]
    # The label the work count is read off the run's own summary by, or None
    # when the work is the fixture itself: a carve-only run over an image with
    # no filesystem scans every byte of it, and a hybrid run reads every byte
    # of the disk it is given.
    work_label: str | None = None


CASES = (
    Case(
        name="scan-throughput",
        unit="MiB/s",
        fixture=Fixture("scan.img", "pattern", (str(SCAN_IMAGE_BYTES), "counter")),
        binary="revenant-carve",
        flags=("--dry-run",),
    ),
    Case(
        name="carve-validate",
        unit="candidates/s",
        fixture=Fixture("carve.img", "carve", (str(CARVE_CORPUS_BYTES),)),
        binary="revenant-carve",
        flags=("--dry-run",),
        work_label="carve candidates",
    ),
    Case(
        name="ntfs-enumerate",
        unit="entries/s",
        fixture=Fixture("ntfs.img", "ntfs", (str(NTFS_MFT_RECORDS),)),
        binary="revenant-undelete",
        flags=("--fs-only", "--dry-run"),
        work_label="filesystem entries",
    ),
    Case(
        name="end-to-end-hybrid",
        unit="MiB/s",
        fixture=Fixture("disk.img", "disk"),
        binary="revenant-undelete",
        flags=("--hybrid", "--dry-run"),
    ),
)


def selected(name_filter: str | None) -> tuple[Case, ...]:
    """Every case, or only those whose name contains `name_filter`."""
    if name_filter is None:
        return CASES
    return tuple(case for case in CASES if name_filter in case.name)
