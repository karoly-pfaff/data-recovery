#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate seed inputs for the NTFS libFuzzer corpora.

An empty corpus makes the fuzz gate hollow: libFuzzer has to synthesise the
`FILE` / `NTFS` magic *and* a self-consistent header before it reaches any
parsing code, which a short CI run will not do. Seeding with one structurally
valid input per parser puts the fuzzer inside the interesting state space
immediately, so mutation explores field values instead of magic bytes.

Run from the repository root:

    python3 tools/fuzz/make_seed_corpus.py

The layouts mirror tests/support/MftRecordTestSupport.cpp and the boot sector
built in tests/unit/fs/ntfs/BootSectorTest.cpp.
"""
from __future__ import annotations

import struct
from pathlib import Path

CORPUS_ROOT = Path("tests/fuzz/corpus")
RECORD_SIZE = 1024
BOOT_SECTOR_SIZE = 512


def put(buf: bytearray, offset: int, raw: bytes) -> None:
    buf[offset : offset + len(raw)] = raw


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


def standard_information(buf: bytearray, offset: int) -> None:
    put(buf, offset, struct.pack("<I", 0x10))
    put(buf, offset + 0x04, struct.pack("<I", 0x48))
    put(buf, offset + 0x0A, struct.pack("<H", 0x40))
    put(buf, offset + 0x10, struct.pack("<I", 0x30))
    put(buf, offset + 0x14, struct.pack("<H", 0x18))
    for i, stamp in enumerate((0x1111, 0x2222, 0x3333, 0x4444)):
        put(buf, offset + 0x18 + (i * 8), struct.pack("<Q", stamp))


def file_name(buf: bytearray, offset: int) -> None:
    name = "photo.jpg".encode("utf-16-le")
    content_length = 0x42 + len(name)
    put(buf, offset, struct.pack("<I", 0x30))
    put(buf, offset + 0x04, struct.pack("<I", ((0x18 + content_length + 7) // 8) * 8))
    put(buf, offset + 0x0A, struct.pack("<H", 0x40))
    put(buf, offset + 0x10, struct.pack("<I", content_length))
    put(buf, offset + 0x14, struct.pack("<H", 0x18))
    body = offset + 0x18
    put(buf, body, struct.pack("<Q", 5 | (1 << 48)))  # parent record 5, sequence 1
    put(buf, body + 0x40, struct.pack("<B", len(name) // 2))
    put(buf, body + 0x41, struct.pack("<B", 1))  # Win32 namespace
    put(buf, body + 0x42, name)


def resident_data(buf: bytearray, offset: int) -> None:
    payload = b"hello-ntfs"
    put(buf, offset, struct.pack("<I", 0x80))
    put(buf, offset + 0x04, struct.pack("<I", ((0x18 + len(payload) + 7) // 8) * 8))
    put(buf, offset + 0x0A, struct.pack("<H", 0x40))
    put(buf, offset + 0x10, struct.pack("<I", len(payload)))
    put(buf, offset + 0x14, struct.pack("<H", 0x18))
    put(buf, offset + 0x18, payload)


def mft_record(flags: int) -> bytes:
    """1024 bytes: valid USA plus $STANDARD_INFORMATION, $FILE_NAME, $DATA."""
    buf = bytearray(RECORD_SIZE)
    put(buf, 0x00, b"FILE")
    put(buf, 0x04, struct.pack("<H", 0x30))  # update-sequence array offset
    put(buf, 0x06, struct.pack("<H", 3))  # USN + one entry per 512-byte stride
    put(buf, 0x10, struct.pack("<H", 1))  # sequence number
    put(buf, 0x14, struct.pack("<H", 0x38))  # first attribute offset
    put(buf, 0x16, struct.pack("<H", flags))
    put(buf, 0x18, struct.pack("<I", 0x118))  # used size
    usn = 0x1234
    put(buf, 0x30, struct.pack("<H", usn))
    put(buf, 0x32, struct.pack("<HH", 0xABCD, 0xEF01))  # saved stride tails
    put(buf, 0x1FE, struct.pack("<H", usn))
    put(buf, 0x3FE, struct.pack("<H", usn))
    standard_information(buf, 0x38)
    file_name(buf, 0x80)
    resident_data(buf, 0xF0)
    return bytes(buf)


def write(target: str, name: str, payload: bytes) -> None:
    directory = CORPUS_ROOT / target
    directory.mkdir(parents=True, exist_ok=True)
    (directory / name).write_bytes(payload)
    print(f"{directory / name}: {len(payload)} bytes")


def main() -> int:
    write("NtfsBootSectorFuzz", "valid-boot-sector.bin", boot_sector())
    write("MftRecordFuzz", "in-use-record.bin", mft_record(0x01))
    write("MftRecordFuzz", "deleted-record.bin", mft_record(0x00))
    write("MftRecordFuzz", "directory-record.bin", mft_record(0x03))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
