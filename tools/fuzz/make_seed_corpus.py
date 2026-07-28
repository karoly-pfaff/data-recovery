#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate seed inputs for the filesystem libFuzzer corpora.

An empty corpus makes the fuzz gate hollow: libFuzzer has to synthesise the
`FILE` / `NTFS` magic *and* a self-consistent header before it reaches any
parsing code, which a short CI run will not do. Seeding with one structurally
valid input per parser puts the fuzzer inside the interesting state space
immediately, so mutation explores field values instead of magic bytes.

Run from the repository root:

    python3 tools/fuzz/make_seed_corpus.py

The layouts mirror tests/support/MftRecordTestSupport.cpp and the boot sector
built in tests/unit/fs/ntfs/BootSectorTest.cpp. The `$MFT` region mirrors the
geometry hard-coded in tests/fuzz/NtfsEnumerateFuzz.cpp; the two must agree or
the seed decays into random bytes.
"""
from __future__ import annotations

import struct
from functools import partial
from pathlib import Path
from typing import Callable

CORPUS_ROOT = Path("tests/fuzz/corpus")
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


def aligned(length: int) -> int:
    """An attribute's length, padded to the 8-byte multiple the walker wants."""
    return ((length + 7) // 8) * 8


def standard_information(buf: bytearray, offset: int) -> int:
    length = 0x48
    put(buf, offset, struct.pack("<I", 0x10))
    put(buf, offset + 0x04, struct.pack("<I", length))
    put(buf, offset + 0x0A, struct.pack("<H", 0x40))
    put(buf, offset + 0x10, struct.pack("<I", 0x30))
    put(buf, offset + 0x14, struct.pack("<H", 0x18))
    for i, stamp in enumerate((0x1111, 0x2222, 0x3333, 0x4444)):
        put(buf, offset + 0x18 + (i * 8), struct.pack("<Q", stamp))
    return length


def file_name(
    buf: bytearray, offset: int, name: str = "photo.jpg", parent: int = 5
) -> int:
    encoded = name.encode("utf-16-le")
    content_length = 0x42 + len(encoded)
    length = aligned(0x18 + content_length)
    put(buf, offset, struct.pack("<I", 0x30))
    put(buf, offset + 0x04, struct.pack("<I", length))
    put(buf, offset + 0x0A, struct.pack("<H", 0x40))
    put(buf, offset + 0x10, struct.pack("<I", content_length))
    put(buf, offset + 0x14, struct.pack("<H", 0x18))
    body = offset + 0x18
    put(buf, body, struct.pack("<Q", parent | (1 << 48)))  # reference sequence 1
    put(buf, body + 0x40, struct.pack("<B", len(encoded) // 2))
    put(buf, body + 0x41, struct.pack("<B", 1))  # Win32 namespace
    put(buf, body + 0x42, encoded)
    return length


def resident_data(buf: bytearray, offset: int, payload: bytes = b"hello-ntfs") -> int:
    length = aligned(0x18 + len(payload))
    put(buf, offset, struct.pack("<I", 0x80))
    put(buf, offset + 0x04, struct.pack("<I", length))
    put(buf, offset + 0x0A, struct.pack("<H", 0x40))
    put(buf, offset + 0x10, struct.pack("<I", len(payload)))
    put(buf, offset + 0x14, struct.pack("<H", 0x18))
    put(buf, offset + 0x18, payload)
    return length


def non_resident_data(
    buf: bytearray, offset: int, runs: list[tuple[int, int]], real_size: int
) -> int:
    """A `$DATA` whose content lives in `runs`, at REGION_CLUSTER_BYTES each."""
    encoded = runlist(runs)
    length = aligned(0x40 + len(encoded))
    clusters = sum(count for count, _ in runs)
    put(buf, offset, struct.pack("<I", 0x80))
    put(buf, offset + 0x04, struct.pack("<I", length))
    put(buf, offset + 0x08, struct.pack("<B", 1))  # non-resident
    put(buf, offset + 0x0A, struct.pack("<H", 0x40))
    put(buf, offset + 0x18, struct.pack("<Q", clusters - 1))  # last VCN
    put(buf, offset + 0x20, struct.pack("<H", 0x40))  # runlist offset
    put(buf, offset + 0x28, struct.pack("<Q", clusters * REGION_CLUSTER_BYTES))
    put(buf, offset + 0x30, struct.pack("<Q", real_size))
    put(buf, offset + 0x38, struct.pack("<Q", real_size))
    put(buf, offset + 0x40, encoded)
    return length


def record_header(buf: bytearray, flags: int, used: int) -> None:
    put(buf, 0x00, b"FILE")
    put(buf, 0x04, struct.pack("<H", 0x30))  # update-sequence array offset
    put(buf, 0x06, struct.pack("<H", 3))  # USN + one entry per 512-byte stride
    put(buf, 0x10, struct.pack("<H", 1))  # sequence number
    put(buf, 0x14, struct.pack("<H", 0x38))  # first attribute offset
    put(buf, 0x16, struct.pack("<H", flags))
    put(buf, 0x18, struct.pack("<I", used))


def update_sequence(buf: bytearray) -> None:
    """Stamps the USN into both stride tails, past every attribute we write."""
    usn = 0x1234
    put(buf, 0x30, struct.pack("<H", usn))
    put(buf, 0x32, struct.pack("<HH", 0xABCD, 0xEF01))  # saved stride tails
    put(buf, 0x1FE, struct.pack("<H", usn))
    put(buf, 0x3FE, struct.pack("<H", usn))


def record(attributes: list[AttributeWriter], flags: int) -> bytes:
    """1024 bytes: a valid USA plus `attributes`, laid out back to back."""
    buf = bytearray(RECORD_SIZE)
    offset = 0x38
    for attribute in attributes:
        offset += attribute(buf, offset)
    record_header(buf, flags, offset)
    update_sequence(buf)
    return bytes(buf)


def mft_record(flags: int) -> bytes:
    """$STANDARD_INFORMATION, $FILE_NAME, and a resident $DATA."""
    return record([standard_information, file_name, resident_data], flags)


def named_record(
    name: str, parent: int, flags: int, data: AttributeWriter | None = None
) -> bytes:
    attributes: list[AttributeWriter] = [
        standard_information,
        partial(file_name, name=name, parent=parent),
    ]
    if data is not None:
        attributes.append(data)
    return record(attributes, flags)


def runlist(runs: list[tuple[int, int]]) -> bytes:
    """Encode `(length_clusters, lcn_delta)` pairs as NTFS data runs.

    A delta of 0 is written with a zero-width offset field, which is how NTFS
    spells a sparse run.
    """
    out = bytearray()
    for length, delta in runs:
        length_field = length.to_bytes((length.bit_length() + 7) // 8 or 1, "little")
        offset_field = b"" if delta == 0 else delta.to_bytes(2, "little", signed=True)
        out.append((len(offset_field) << 4) | len(length_field))
        out += length_field + offset_field
    out.append(0x00)
    return bytes(out)


def mft_region() -> bytes:
    """A whole tiny `$MFT`: a self-describing record 0, a root, and four files.

    Reaching the enumeration at all needs record 0 to map its own runlist, so a
    seed of anything less never gets past `MftTable::open`.
    """
    mft_bytes = RECORD_SIZE * REGION_RECORD_COUNT
    records = {
        0: named_record(
            "$MFT",
            5,
            0x01,
            partial(
                non_resident_data,
                runs=[(REGION_RECORD_COUNT, REGION_MFT_CLUSTER)],
                real_size=mft_bytes,
            ),
        ),
        5: named_record(".", 5, 0x03),
        16: named_record("photos", 5, 0x03),
        17: named_record(
            "photo.jpg",
            16,
            0x00,
            partial(non_resident_data, runs=[(1, 21)], real_size=900),
        ),
        18: named_record(
            "notes.txt",
            5,
            0x00,
            partial(resident_data, payload=b"deleted but resident"),
        ),
        19: named_record("orphan.jpg", 99, 0x00, partial(non_resident_data, runs=[(1, 22)], real_size=500)),
    }
    start = REGION_MFT_CLUSTER * REGION_CLUSTER_BYTES
    region = bytearray(start + mft_bytes)
    for number, raw in records.items():
        put(region, start + (number * RECORD_SIZE), raw)
    return bytes(region)


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
    write("RunlistFuzz", "fragmented-runlist.bin", runlist([(24, 0x5634), (8, -0x100)]))
    write("RunlistFuzz", "sparse-runlist.bin", runlist([(4, 0x20), (5, 0), (2, 0x10)]))
    write("NtfsEnumerateFuzz", "mft-region.bin", mft_region())
    write("Fat32BootSectorFuzz", "valid-boot-sector.bin", fat32_boot_sector())
    write(
        "FatDirectoryEntryFuzz",
        "live-file.bin",
        fat_short_entry(b"KEEP    JPG", 0x20, 0x12345, 9000),
    )
    write(
        "FatDirectoryEntryFuzz",
        "deleted-file.bin",
        fat_short_entry(b"\xE5EEP    JPG", 0x20, 0x12345, 9000),
    )
    write(
        "FatDirectoryEntryFuzz",
        "long-name-fragment.bin",
        fat_long_name_fragment(0x41, "recovered.jpg"),
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
