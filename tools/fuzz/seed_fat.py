#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""FAT directory entries: the 8.3 short entry and a long-name fragment."""
from __future__ import annotations

import struct

from seed_primitives import put


def fat_short_entry(name: bytes, attributes: int, cluster: int, size: int) -> bytes:
    """One 32-byte FAT directory slot describing a file."""
    buf = bytearray(32)
    put(buf, 0x00, name)
    put(buf, 0x0B, struct.pack("<B", attributes))
    put(buf, 0x0E, struct.pack("<HH", 0x6000, 0x5100))  # created 12:00, 2020-08-01
    put(buf, 0x14, struct.pack("<H", cluster >> 16))
    put(buf, 0x16, struct.pack("<HH", 0x6000, 0x5100))
    put(buf, 0x1A, struct.pack("<H", cluster & 0xFFFF))
    put(buf, 0x1C, struct.pack("<I", size))
    return bytes(buf)


def fat_long_name_fragment(ordinal: int, text: str) -> bytes:
    """One 32-byte long-name slot holding up to 13 UTF-16 code units."""
    buf = bytearray(32)
    put(buf, 0x00, struct.pack("<B", ordinal))
    put(buf, 0x0B, struct.pack("<B", 0x0F))
    put(buf, 0x0D, struct.pack("<B", 0x5A))
    encoded = text.encode("utf-16-le").ljust(26, b"\xFF")
    put(buf, 0x01, encoded[:10])
    put(buf, 0x0E, encoded[10:22])
    put(buf, 0x1C, encoded[22:26])
    return bytes(buf)


