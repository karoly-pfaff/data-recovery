#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The bytes every seed builder puts down, and the geometry they share.

Split out of `make_seed_corpus.py` (story-0703), which had grown to 763 lines —
three times the limit and the largest source file in the tree. The split is by
responsibility: one module per format family, and the corpus manifest stays in
the driver.
"""
from __future__ import annotations

from typing import Callable

RECORD_SIZE = 1024
BOOT_SECTOR_SIZE = 512

# The tiny volume NtfsEnumerateFuzz mounts its input as.
REGION_CLUSTER_BYTES = 1024
REGION_MFT_CLUSTER = 1
REGION_RECORD_COUNT = 20

# An attribute writer, bound to its own content and applied at an offset.
AttributeWriter = Callable[[bytearray, int], int]


def put(buf: bytearray, offset: int, raw: bytes) -> None:
    buf[offset : offset + len(raw)] = raw


