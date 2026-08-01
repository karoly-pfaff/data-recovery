#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The kernel side of story-0603's pass: a backing file as a real block device.

`losetup` turns a synthetic image into `/dev/loopN`, which is the only way this
project can execute `RawDevicePosix.cpp` at all — CI runners hand out no block
devices and Windows cannot pretend to be one.

Two answers here are the pass's second witness. `losetup -P` makes the kernel
parse the same MBR we do, so `partition_sizes` is an independent reading of the
table our listing prints; and `--sector-size` makes `BLKSSZGET` answer 4096,
which is the only way to run story-0401's alignment arithmetic at 4Kn geometry.

Everything needs root. `attached` is a context manager because a `losetup -d`
that runs only when the last check passed is precisely the leak that leaves a
stale `/dev/loopN` for the next session.
"""
from __future__ import annotations

import subprocess
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path


class LoopError(RuntimeError):
    """losetup or lsblk refused; the pass cannot continue without the device."""


def _run(command: list[str]) -> str:
    finished = subprocess.run(command, capture_output=True, text=True, check=False)
    if finished.returncode != 0:
        raise LoopError(f"{' '.join(command)} failed: {finished.stderr.strip()}")
    return finished.stdout.strip()


def attach(
    backing: Path,
    *,
    partition_scan: bool = False,
    sector_size: int | None = None,
    read_only: bool = False,
) -> str:
    """`read_only` is `losetup -r`: the kernel refuses writes to the node.

    The pass uses it to ask a question the digest cannot — not whether anything
    wrote to the source, but whether anything so much as asked for write access.
    An `open(O_RDWR)` on such a node fails, so a run that survives it never
    requested one (ADR-0011's structural half).
    """
    command = ["losetup", "--show", "-f"]
    if partition_scan:
        command.append("-P")
    if read_only:
        command.append("-r")
    if sector_size is not None:
        command += ["--sector-size", str(sector_size)]
    return _run([*command, str(backing)])


def detach(device: str) -> None:
    _run(["losetup", "-d", device])


@contextmanager
def attached(
    backing: Path,
    *,
    partition_scan: bool = False,
    sector_size: int | None = None,
    read_only: bool = False,
) -> Iterator[str]:
    device = attach(
        backing, partition_scan=partition_scan, sector_size=sector_size, read_only=read_only
    )
    try:
        yield device
    finally:
        detach(device)


def is_read_only(device: str) -> bool:
    """What `BLKROGET` answers — whether the kernel will refuse a write.

    Asked rather than assumed: `read_only=True` on the attachment is an
    argument, and a check that leans on it has to see it took effect. Without
    this the read-only verdict would pass on a writable device.
    """
    return _run(["blockdev", "--getro", device]) == "1"


def sector_size(device: str) -> int:
    """What `BLKSSZGET` answers — the same ioctl `RawDevicePosix` calls."""
    return int(_run(["blockdev", "--getss", device]))


def size_bytes(device: str) -> int:
    """What `BLKGETSIZE64` answers."""
    return int(_run(["blockdev", "--getsize64", device]))


def partition_sizes(device: str) -> list[int]:
    """The kernel's own scan of the table, in partition order.

    `lsblk -b` prints the whole device first and its partitions after, so the
    first line is dropped: what is wanted is the four lengths to hold against
    the four our listing prints.
    """
    lines = _run(["lsblk", "-b", "--noheadings", "--output", "SIZE", device]).splitlines()
    return [int(line.strip()) for line in lines[1:]]


def node_mode(device: str) -> str:
    """`user:group mode` of the device node, as the pass records it."""
    return _run(["stat", "-c", "%U:%G %a", device])
