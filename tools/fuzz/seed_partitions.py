#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""MBR and GPT disks — the partition tables `volume/` parses."""
from __future__ import annotations

import struct
import zlib

from seed_primitives import put


MBR_SECTOR_SIZE = 512
MBR_TABLE_OFFSET = 0x1BE
MBR_ENTRY_BYTES = 16
MBR_EXTENDED_LBA = 4
MBR_SECOND_EBR_LBA = 8


def mbr_signature(buf: bytearray, lba: int) -> None:
    put(buf, (lba * MBR_SECTOR_SIZE) + 0x1FE, b"\x55\xAA")


def mbr_slot(buf: bytearray, lba: int, index: int, slot: tuple[int, int, int]) -> None:
    """One 16-byte table slot: type byte, starting LBA, sector count.

    The status byte is left zero and the two CHS triples are left zero; neither
    is read, but a non-zero status would fail the table's own validity rule.
    """
    type_code, start_lba, sectors = slot
    at = (lba * MBR_SECTOR_SIZE) + MBR_TABLE_OFFSET + (index * MBR_ENTRY_BYTES)
    put(buf, at + 0x04, struct.pack("<B", type_code))
    put(buf, at + 0x08, struct.pack("<I", start_lba))
    put(buf, at + 0x0C, struct.pack("<I", sectors))


def mbr_disk() -> bytes:
    """A partitioned disk: one primary partition plus an extended one holding a
    two-link EBR chain.

    Reaching the chain walk at all needs sector 0 to validate, and reaching the
    *second* link needs slot 1 to be read against the extended partition's head
    rather than the current EBR — so a seed of anything less never exercises the
    part of this parser that is worth fuzzing.
    """
    disk = bytearray(16 * MBR_SECTOR_SIZE)
    mbr_signature(disk, 0)
    mbr_slot(disk, 0, 0, (0x07, 12, 4))
    mbr_slot(disk, 0, 1, (0x05, MBR_EXTENDED_LBA, 8))
    mbr_signature(disk, MBR_EXTENDED_LBA)
    mbr_slot(disk, MBR_EXTENDED_LBA, 0, (0x83, 1, 2))
    mbr_slot(disk, MBR_EXTENDED_LBA, 1, (0x05, MBR_SECOND_EBR_LBA - MBR_EXTENDED_LBA, 4))
    mbr_signature(disk, MBR_SECOND_EBR_LBA)
    mbr_slot(disk, MBR_SECOND_EBR_LBA, 0, (0x83, 1, 2))
    return bytes(disk)


GPT_HEADER_BYTES = 92
GPT_ENTRY_BYTES = 128
GPT_ENTRY_COUNT = 4
GPT_DISK_SECTORS = 64
GPT_FIRST_USABLE = 34


def gpt_entry(type_seed: int, first: int, last: int, name: str) -> bytes:
    """One 128-byte partition entry. The type GUID need not be a real one."""
    buf = bytearray(GPT_ENTRY_BYTES)
    put(buf, 0x00, bytes([type_seed]) * 16)
    put(buf, 0x20, struct.pack("<Q", first))
    put(buf, 0x28, struct.pack("<Q", last))
    put(buf, 0x38, name.encode("utf-16-le"))
    return bytes(buf)


def gpt_entry_array() -> bytes:
    """Four slots, two of them used."""
    buf = bytearray(GPT_ENTRY_COUNT * GPT_ENTRY_BYTES)
    put(buf, 0, gpt_entry(0xA1, GPT_FIRST_USABLE, 40, "System"))
    put(buf, GPT_ENTRY_BYTES, gpt_entry(0xB2, 41, 47, "Data"))
    return bytes(buf)


def gpt_header(my_lba: int, alternate_lba: int, array_lba: int, array_crc: int) -> bytes:
    """A header, checksummed last with its own checksum field taken as zero.

    Without a matching CRC the parser stops at the checksum and never reaches a
    single field behind it, so an unchecksummed seed is worth about as much as
    random bytes.
    """
    buf = bytearray(GPT_HEADER_BYTES)
    put(buf, 0x00, b"EFI PART")
    put(buf, 0x08, struct.pack("<I", 0x00010000))
    put(buf, 0x0C, struct.pack("<I", GPT_HEADER_BYTES))
    put(buf, 0x18, struct.pack("<Q", my_lba))
    put(buf, 0x20, struct.pack("<Q", alternate_lba))
    put(buf, 0x28, struct.pack("<Q", GPT_FIRST_USABLE))
    put(buf, 0x30, struct.pack("<Q", GPT_DISK_SECTORS - 3))
    put(buf, 0x48, struct.pack("<Q", array_lba))
    put(buf, 0x50, struct.pack("<I", GPT_ENTRY_COUNT))
    put(buf, 0x54, struct.pack("<I", GPT_ENTRY_BYTES))
    put(buf, 0x58, struct.pack("<I", array_crc))
    put(buf, 0x10, struct.pack("<I", zlib.crc32(bytes(buf)) & 0xFFFFFFFF))
    return bytes(buf)


def gpt_disk() -> bytes:
    """A GPT-partitioned disk: a protective MBR, and both copies of the table."""
    disk = bytearray(GPT_DISK_SECTORS * MBR_SECTOR_SIZE)
    array = gpt_entry_array()
    crc = zlib.crc32(array) & 0xFFFFFFFF
    backup = GPT_DISK_SECTORS - 1
    mbr_signature(disk, 0)
    mbr_slot(disk, 0, 0, (0xEE, 1, GPT_DISK_SECTORS - 1))
    put(disk, 2 * MBR_SECTOR_SIZE, array)
    put(disk, 1 * MBR_SECTOR_SIZE, gpt_header(1, backup, 2, crc))
    put(disk, (backup - 1) * MBR_SECTOR_SIZE, array)
    put(disk, backup * MBR_SECTOR_SIZE, gpt_header(backup, 1, backup - 1, crc))
    return bytes(disk)
