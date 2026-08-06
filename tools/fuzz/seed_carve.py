#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Carve-format seeds and the shared machinery inputs (story-0606).

Eleven corpora held nothing but a `.gitkeep` before that story: all six format
carvers and five pieces of shared machinery. The layouts mirror the unit tests
that build the same shapes in C++ — PngCarverTest's `minimalPng`,
FixtureJpeg.cpp's frame — the same way the NTFS seeds mirror
MftRecordTestSupport.cpp. If either side moves, the seed decays into random
bytes and the corpus quietly stops being a seed.
"""
from __future__ import annotations

import struct
import zlib


JPEG_FRAME_BYTES = 14
ENTROPY_MODULUS = 0xFE  # never produces a raw 0xFF, which would read as a marker


def fixture_jpeg(size_bytes: int) -> bytes:
    """SOI, a 6-byte APP0, a 4-byte SOS, entropy, EOI — FixtureJpeg.cpp's shape."""
    head = bytes([0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x04, 0x4A, 0x46, 0xFF, 0xDA, 0x00, 0x02])
    entropy = bytes(i % ENTROPY_MODULUS for i in range(size_bytes - JPEG_FRAME_BYTES))
    return head + entropy + bytes([0xFF, 0xD9])


def png_chunk(kind: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(kind + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", crc)


def minimal_png() -> bytes:
    """Signature, IHDR, one IDAT and IEND — PngCarverTest's `minimalPng`."""
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", bytes([1]) * 13)
        + png_chunk(b"IDAT", bytes([0x77]) * 20)
        + png_chunk(b"IEND", b"")
    )


def stored_zip() -> bytes:
    """One stored entry, its central directory record, and the end record."""
    name, body = b"a.txt", b"revenant"
    crc = zlib.crc32(body) & 0xFFFFFFFF
    local = (
        struct.pack(
            "<IHHHHHIIIHH", 0x04034B50, 20, 0, 0, 0, 0, crc, len(body), len(body), len(name), 0
        )
        + name
        + body
    )
    central = (
        struct.pack(
            "<IHHHHHHIIIHHHHHII",
            0x02014B50, 20, 20, 0, 0, 0, 0, crc, len(body), len(body), len(name),
            0, 0, 0, 0, 0, 0,
        )
        + name
    )
    end = struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, 1, 1, len(central), len(local), 0)
    return local + central + end


def minimal_pdf() -> bytes:
    return (
        b"%PDF-1.7\n"
        b"1 0 obj\n<< /Type /Catalog >>\nendobj\n"
        b"trailer\n<< /Size 2 /Root 1 0 R >>\n"
        b"startxref\n9\n%%EOF\n"
    )


def mp4_box(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", 8 + len(payload)) + kind + payload


def minimal_mp4() -> bytes:
    """`ftyp` at offset 4 — Mp4Carver's signature — then a moov and an mdat."""
    ftyp = mp4_box(b"ftyp", b"isom" + struct.pack(">I", 512) + b"isomiso2")
    return ftyp + mp4_box(b"moov", bytes(16)) + mp4_box(b"mdat", bytes(32))


def tiff_header(big_endian: bool) -> bytes:
    """RawCarver carves TIFF: `II*\\0` or `MM\\0*`, then one IFD with one entry."""
    end = ">" if big_endian else "<"
    magic = b"MM\x00\x2a" if big_endian else b"II\x2a\x00"
    entry = struct.pack(end + "HHIHH", 0x0100, 3, 1, 64, 0)  # ImageWidth, SHORT
    ifd = struct.pack(end + "H", 1) + entry + struct.pack(end + "I", 0)
    return magic + struct.pack(end + "I", 8) + ifd


def byte_reader_input(offset: int, payload_bytes: int) -> bytes:
    """ByteReaderFuzz reads an LE64 offset from the first eight bytes."""
    return struct.pack("<Q", offset) + bytes(range(payload_bytes))


def signature_scan_input(lead: int, length: int, tail: int) -> bytes:
    """SignatureScanFuzz's in-target carver: magic 0xAB 0xCD, LE16 length at +2."""
    return bytes(lead) + b"\xab\xcd" + struct.pack("<H", length) + bytes(range(tail))
