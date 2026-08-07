#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""ext4 seeds: superblock, inode, extent tree, directory entry, journal, volume.

`Ext4EnumerateFuzz/volume.bin` is the one that drifted from this generator for
months and survived every CI run — `.` and `..` carried inode 12 where ext4's
root is 2. `tests/unit/lint/test_seed_corpus.py` is what now holds them together.
"""
from __future__ import annotations

import struct

from seed_primitives import put


EXT4_INODE_SIZE = 256
EXT4_TIMESTAMP = 1596283200
EXT4_EXTENTS_FL = 0x80000


def ext4_inode_head(mode: int, links: int, size: int, block_map: bytes) -> bytearray:
    """The fixed fields every ext4 inode carries, plus its block map.

    `ext4_inode` and `ext4_inode_record` differ in what goes in `i_block` and in
    the tail they add after it — not in this header. It was written out twice
    until the duplication gate started reading Python (story-0703).
    """
    buf = bytearray(EXT4_INODE_SIZE)
    put(buf, 0x00, struct.pack("<H", mode))
    put(buf, 0x04, struct.pack("<I", size))
    put(buf, 0x08, struct.pack("<I", EXT4_TIMESTAMP))  # i_atime
    put(buf, 0x10, struct.pack("<I", EXT4_TIMESTAMP))  # i_mtime
    put(buf, 0x1A, struct.pack("<H", links))
    put(buf, 0x20, struct.pack("<I", EXT4_EXTENTS_FL))
    put(buf, 0x28, block_map)
    return buf


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
    buf = ext4_inode_head(mode, links, 9000, ext4_extent_node(1, 0))
    put(buf, 0x80, struct.pack("<H", 32))  # i_extra_isize
    put(buf, 0x90, struct.pack("<I", EXT4_TIMESTAMP))  # i_crtime
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
    return bytes(ext4_inode_head(mode, links, size, ext4_extent_tree(runs)))


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
