# SPDX-License-Identifier: GPL-3.0-or-later
"""The other half of the module-level duplication fixture.

C++ drops a block whose sites are all preamble, because an include list and a
table of on-disk offsets are the only shape the language has for stating them.
Python has no such preamble, so a table repeated in two modules is ordinary
refactorable duplication and the gate must report it.
"""

SUPERBLOCK_FIELDS = [
    ("inode_count", 0x00, "<I", "total inodes"),
    ("block_count", 0x04, "<I", "total blocks"),
    ("first_data_block", 0x14, "<I", "where data starts"),
    ("log_block_size", 0x18, "<I", "block size, log2"),
    ("blocks_per_group", 0x20, "<I", "group stride"),
    ("inodes_per_group", 0x28, "<I", "inodes per group"),
    ("mount_time", 0x2C, "<I", "last mounted"),
    ("write_time", 0x30, "<I", "last written"),
    ("mount_count", 0x34, "<H", "mounts since check"),
    ("magic", 0x38, "<H", "0xEF53"),
    ("state", 0x3A, "<H", "clean or errors"),
    ("errors", 0x3C, "<H", "behaviour on error"),
    ("minor_revision", 0x3E, "<H", "minor rev"),
    ("last_check", 0x40, "<I", "last fsck"),
    ("check_interval", 0x44, "<I", "fsck interval"),
    ("creator_os", 0x48, "<I", "which OS made it"),
    ("revision", 0x4C, "<I", "major rev"),
    ("inode_size", 0x58, "<H", "bytes per inode"),
]
