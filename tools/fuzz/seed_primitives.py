#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The one thing every seed builder does: put bytes at an offset.

Split out of `make_seed_corpus.py` (story-0703), which had grown to 763 lines.
Only what more than one builder uses lives here — the NTFS geometry and the
boot-sector size went to the modules that are actually about them.
"""
from __future__ import annotations


def put(buf: bytearray, offset: int, raw: bytes) -> None:
    buf[offset : offset + len(raw)] = raw
