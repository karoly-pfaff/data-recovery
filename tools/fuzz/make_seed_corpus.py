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


def ext4_superblock() -> bytes:
    """1024 bytes: the superblock tests/unit/fs/ext4/SuperblockTest.cpp asserts
    on. 4096-byte blocks, two groups, 256-byte inodes, extents, no 64-bit."""
    buf = bytearray(1024)
    put(buf, 0x00, struct.pack("<I", 2048))  # s_inodes_count
    put(buf, 0x04, struct.pack("<I", 16384))  # s_blocks_count_lo
    put(buf, 0x14, struct.pack("<I", 0))  # s_first_data_block
    put(buf, 0x18, struct.pack("<I", 2))  # 1024 << 2 == 4096
    put(buf, 0x20, struct.pack("<I", 8192))  # s_blocks_per_group
    put(buf, 0x28, struct.pack("<I", 1024))  # s_inodes_per_group
    put(buf, 0x38, struct.pack("<H", 0xEF53))  # s_magic
    put(buf, 0x58, struct.pack("<H", 256))  # s_inode_size
    put(buf, 0x60, struct.pack("<I", 0x40))  # INCOMPAT_EXTENTS
    put(buf, 0x64, struct.pack("<I", 27))  # s_last_orphan
    return bytes(buf)


def ext4_extent_node(entries: int, depth: int) -> bytes:
    """The 60 bytes an inode gives its extent tree: a header and its entries."""
    buf = bytearray(60)
    put(buf, 0x00, struct.pack("<H", 0xF30A))  # eh_magic
    put(buf, 0x02, struct.pack("<H", entries))
    put(buf, 0x04, struct.pack("<H", 4))  # eh_max
    put(buf, 0x06, struct.pack("<H", depth))
    for index in range(entries):
        at = 12 + (index * 12)
        put(buf, at + 0x00, struct.pack("<I", index * 4))  # ee_block / ei_block
        put(buf, at + 0x04, struct.pack("<H", 4))  # ee_len / ei_leaf_lo low half
        put(buf, at + 0x08, struct.pack("<I", 100 + (index * 4)))  # ee_start_lo
    return bytes(buf)


def ext4_inode(mode: int, links: int) -> bytes:
    """A 256-byte inode with an extent tree in its block map."""
    buf = bytearray(256)
    put(buf, 0x00, struct.pack("<H", mode))
    put(buf, 0x04, struct.pack("<I", 9000))  # i_size_lo
    put(buf, 0x08, struct.pack("<I", 1596283200))  # i_atime
    put(buf, 0x10, struct.pack("<I", 1596283200))  # i_mtime
    put(buf, 0x1A, struct.pack("<H", links))
    put(buf, 0x20, struct.pack("<I", 0x80000))  # EXT4_EXTENTS_FL
    put(buf, 0x28, ext4_extent_node(1, 0))  # i_block
    put(buf, 0x80, struct.pack("<H", 32))  # i_extra_isize
    put(buf, 0x90, struct.pack("<I", 1596283200))  # i_crtime
    return bytes(buf)


def ext4_dir_entry(name: bytes, file_type: int, record: int, inode: int = 12) -> bytes:
    """One linear directory entry, padded out to its record length."""
    buf = bytearray(record)
    put(buf, 0x00, struct.pack("<I", inode))
    put(buf, 0x04, struct.pack("<H", record))
    put(buf, 0x06, struct.pack("<B", len(name)))
    put(buf, 0x07, struct.pack("<B", file_type))
    put(buf, 0x08, name)
    return bytes(buf)


def ext4_journal_superblock() -> bytes:
    """A jbd2 superblock: big-endian, 1024-byte blocks, no extra features."""
    buf = bytearray(1024)
    put(buf, 0x00, struct.pack(">I", 0xC03B3998))  # h_magic
    put(buf, 0x04, struct.pack(">I", 4))  # JBD2_SUPERBLOCK_V2
    put(buf, 0x0C, struct.pack(">I", 1024))  # s_blocksize
    put(buf, 0x10, struct.pack(">I", 32))  # s_maxlen
    put(buf, 0x14, struct.pack(">I", 1))  # s_first
    put(buf, 0x18, struct.pack(">I", 1))  # s_sequence
    return bytes(buf)


def ext4_journal_descriptor() -> bytes:
    """A descriptor block announcing one copy of filesystem block 8."""
    buf = bytearray(1024)
    put(buf, 0x00, struct.pack(">I", 0xC03B3998))
    put(buf, 0x04, struct.pack(">I", 1))  # descriptor
    put(buf, 0x08, struct.pack(">I", 1))  # h_sequence
    put(buf, 0x0C, struct.pack(">I", 8))  # t_blocknr
    put(buf, 0x12, struct.pack(">H", 0x0A))  # SAME_UUID | LAST_TAG
    return bytes(buf)


# The tiny ext4 volume Ext4EnumerateFuzz mounts its input as: 1024-byte blocks,
# one block group, an inode table at block 5, a root directory in block 20 and
# one file in block 21. Reaching the walk at all needs all of that to agree, so a
# seed of anything less never gets past the superblock.
EXT4_BLOCK_SIZE = 1024
EXT4_BLOCKS = 64
EXT4_INODES = 32
EXT4_INODE_SIZE = 256
EXT4_INODE_TABLE_BLOCK = 5
EXT4_ROOT_DIR_BLOCK = 20
EXT4_FILE_BLOCK = 21


def ext4_extent_tree(runs: list[tuple[int, int, int]]) -> bytes:
    """The 60 bytes of `i_block`: a header and one leaf per run."""
    buf = bytearray(60)
    if not runs:
        return bytes(buf)
    put(buf, 0x00, struct.pack("<H", 0xF30A))
    put(buf, 0x02, struct.pack("<H", len(runs)))
    put(buf, 0x04, struct.pack("<H", 4))
    for index, (file_block, count, device_block) in enumerate(runs):
        at = 12 + (index * 12)
        put(buf, at + 0x00, struct.pack("<I", file_block))
        put(buf, at + 0x04, struct.pack("<H", count))
        put(buf, at + 0x08, struct.pack("<I", device_block))
    return bytes(buf)


def ext4_inode_record(mode: int, links: int, size: int, runs: list) -> bytes:
    buf = bytearray(EXT4_INODE_SIZE)
    put(buf, 0x00, struct.pack("<H", mode))
    put(buf, 0x04, struct.pack("<I", size))
    put(buf, 0x08, struct.pack("<I", 1596283200))  # i_atime
    put(buf, 0x10, struct.pack("<I", 1596283200))  # i_mtime
    put(buf, 0x1A, struct.pack("<H", links))
    put(buf, 0x20, struct.pack("<I", 0x80000))  # EXT4_EXTENTS_FL
    put(buf, 0x28, ext4_extent_tree(runs))
    return bytes(buf)


def ext4_root_directory() -> bytes:
    """`.`, `..`, and one live file filling the rest of the block."""
    buf = bytearray(EXT4_BLOCK_SIZE)
    put(buf, 0x00, ext4_dir_entry(b".", 2, 12, inode=2))
    put(buf, 0x0C, ext4_dir_entry(b"..", 2, 12, inode=2))
    put(buf, 0x18, ext4_dir_entry(b"keep.txt", 1, EXT4_BLOCK_SIZE - 24, inode=11))
    return bytes(buf)


def ext4_volume() -> bytes:
    """A whole tiny ext4 volume: superblock, group descriptor, inode table,
    a root directory and one file with content."""
    image = bytearray(EXT4_BLOCKS * EXT4_BLOCK_SIZE)
    put(image, EXT4_BLOCK_SIZE, ext4_superblock_for_seed())
    put(image, 2 * EXT4_BLOCK_SIZE + 0x08, struct.pack("<I", EXT4_INODE_TABLE_BLOCK))
    table = EXT4_INODE_TABLE_BLOCK * EXT4_BLOCK_SIZE
    put(
        image,
        table + (1 * EXT4_INODE_SIZE),
        ext4_inode_record(0x41ED, 2, EXT4_BLOCK_SIZE, [(0, 1, EXT4_ROOT_DIR_BLOCK)]),
    )
    put(
        image,
        table + (10 * EXT4_INODE_SIZE),
        ext4_inode_record(0x81A4, 1, 500, [(0, 1, EXT4_FILE_BLOCK)]),
    )
    put(image, EXT4_ROOT_DIR_BLOCK * EXT4_BLOCK_SIZE, ext4_root_directory())
    put(image, EXT4_FILE_BLOCK * EXT4_BLOCK_SIZE, bytes(500))
    return bytes(image)


def ext4_superblock_for_seed() -> bytes:
    """The superblock the tiny volume above describes itself with."""
    buf = bytearray(1024)
    put(buf, 0x00, struct.pack("<I", EXT4_INODES))
    put(buf, 0x04, struct.pack("<I", EXT4_BLOCKS))
    put(buf, 0x14, struct.pack("<I", 1))  # s_first_data_block
    put(buf, 0x18, struct.pack("<I", 0))  # 1024 << 0
    put(buf, 0x20, struct.pack("<I", 8192))
    put(buf, 0x28, struct.pack("<I", EXT4_INODES))
    put(buf, 0x38, struct.pack("<H", 0xEF53))
    put(buf, 0x58, struct.pack("<H", EXT4_INODE_SIZE))
    put(buf, 0x60, struct.pack("<I", 0x40))  # INCOMPAT_EXTENTS
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
    write("Fat32EnumerateFuzz", "boot-sector.bin", fat32_boot_sector())
    write("ExfatBootRegionFuzz", "valid-boot-sector.bin", exfat_boot_sector())
    write("ExfatDirectoryEntryFuzz", "file-entry.bin", exfat_file_entry())
    write("Ext4SuperblockFuzz", "valid-superblock.bin", ext4_superblock())
    write("Ext4InodeFuzz", "live-file.bin", ext4_inode(0x81A4, 1))
    write("Ext4InodeFuzz", "deleted-file.bin", ext4_inode(0x81A4, 0))
    write("Ext4InodeFuzz", "directory.bin", ext4_inode(0x41ED, 2))
    write("Ext4ExtentTreeFuzz", "leaf-node.bin", ext4_extent_node(2, 0))
    write("Ext4ExtentTreeFuzz", "interior-node.bin", ext4_extent_node(1, 1))
    write("Ext4DirectoryEntryFuzz", "file-entry.bin", ext4_dir_entry(b"photo.jpg", 1, 20))
    write("Ext4DirectoryEntryFuzz", "directory-entry.bin", ext4_dir_entry(b"photos", 2, 16))
    write("Ext4JournalFuzz", "journal-superblock.bin", ext4_journal_superblock())
    write("Ext4JournalFuzz", "descriptor-block.bin", ext4_journal_descriptor())
    write("Ext4EnumerateFuzz", "volume.bin", ext4_volume())
    write("MbrFuzz", "partitioned-disk.bin", mbr_disk())
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
