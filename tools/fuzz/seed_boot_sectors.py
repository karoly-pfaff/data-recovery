#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""NTFS, FAT32 and exFAT boot sectors, and the exFAT directory entry.

The layouts mirror tests/unit/fs/ntfs/BootSectorTest.cpp and its FAT and exFAT
neighbours; if either side moves the seed decays into random bytes.
"""
from __future__ import annotations

import struct

from seed_primitives import put

BOOT_SECTOR_SIZE = 512


def boot_sector() -> bytes:
    """512 bytes: 512 B/sector, 8 sectors/cluster, MFT at cluster 4."""
    buf = bytearray(BOOT_SECTOR_SIZE)
    put(buf, 0x03, b"NTFS    ")
    put(buf, 0x0B, struct.pack("<H", 512))
    put(buf, 0x0D, struct.pack("<B", 8))
    put(buf, 0x28, struct.pack("<Q", 16384))
    put(buf, 0x30, struct.pack("<Q", 4))
    put(buf, 0x40, struct.pack("<b", -10))  # 2^10 = 1024 bytes per MFT record
    put(buf, 0x1FE, b"\x55\xAA")
    return bytes(buf)


def fat32_boot_sector() -> bytes:
    """512 bytes: the FAT32 BPB tests/unit/fs/fat/BootSectorTest.cpp asserts on.

    512 B/sector, 4 sectors/cluster, 32 reserved sectors, two 64-sector FATs.
    """
    buf = bytearray(BOOT_SECTOR_SIZE)
    put(buf, 0x0B, struct.pack("<H", 512))
    put(buf, 0x0D, struct.pack("<B", 4))
    put(buf, 0x0E, struct.pack("<H", 32))
    put(buf, 0x10, struct.pack("<B", 2))
    put(buf, 0x20, struct.pack("<I", 4096))
    put(buf, 0x24, struct.pack("<I", 64))
    put(buf, 0x2C, struct.pack("<I", 2))
    put(buf, 0x52, b"FAT32   ")
    put(buf, 0x1FE, b"\x55\xAA")
    return bytes(buf)


def exfat_boot_sector() -> bytes:
    """512 bytes: the exFAT boot sector tests/unit/fs/exfat/BootRegionTest.cpp
    asserts on. 512 B/sector, 8 sectors/cluster, one 64-sector FAT."""
    buf = bytearray(BOOT_SECTOR_SIZE)
    put(buf, 0x03, b"EXFAT   ")
    put(buf, 0x48, struct.pack("<Q", 8192))
    put(buf, 0x50, struct.pack("<I", 128))
    put(buf, 0x54, struct.pack("<I", 64))
    put(buf, 0x58, struct.pack("<I", 256))
    put(buf, 0x5C, struct.pack("<I", 992))
    put(buf, 0x60, struct.pack("<I", 2))
    put(buf, 0x6C, struct.pack("<B", 9))
    put(buf, 0x6D, struct.pack("<B", 3))
    put(buf, 0x6E, struct.pack("<B", 1))
    put(buf, 0x1FE, b"\x55\xAA")
    return bytes(buf)


def exfat_file_entry() -> bytes:
    """The 32-byte entry that opens an exFAT file's set."""
    buf = bytearray(32)
    put(buf, 0x00, struct.pack("<B", 0x85))
    put(buf, 0x01, struct.pack("<B", 2))
    put(buf, 0x08, struct.pack("<I", 0x51006000))
    return bytes(buf)
