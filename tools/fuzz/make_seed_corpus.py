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

**This file is the corpus manifest — which seed goes where.** The bytes each
seed is made of live beside it, one module per format family (story-0703, which
split this file at 763 lines). Every `write` call stays here on purpose:
`tests/unit/lint/test_seed_corpus.py` drives the generator by replacing this
module's `write`, and a call issued from another module would bypass that hook
and quietly stop being checked.
"""
from __future__ import annotations

from pathlib import Path

from seed_boot_sectors import (
    boot_sector,
    exfat_boot_sector,
    exfat_file_entry,
    fat32_boot_sector,
)
from seed_carve import (
    byte_reader_input,
    fixture_jpeg,
    minimal_mp4,
    minimal_pdf,
    minimal_png,
    signature_scan_input,
    stored_zip,
    tiff_header,
)
from seed_ext4 import (
    ext4_dir_entry,
    ext4_extent_node,
    ext4_inode,
    ext4_journal_descriptor,
    ext4_journal_superblock,
    ext4_superblock,
    ext4_superblock_for_seed,
    ext4_volume,
)
from seed_fat import fat_long_name_fragment, fat_short_entry
from seed_ntfs import mft_record, mft_region, named_record, runlist
from seed_partitions import gpt_disk, mbr_disk

CORPUS_ROOT = Path("tests/fuzz/corpus")


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
    write("GptFuzz", "gpt-disk.bin", gpt_disk())
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
    write_carve_and_machinery_seeds()
    return 0


def write_carve_and_machinery_seeds() -> None:
    """The eleven corpora that held only a `.gitkeep` before story-0606."""
    write("JpegCarverFuzz", "valid-jpeg.bin", fixture_jpeg(4096))
    write("JpegCarverFuzz", "truncated-jpeg.bin", fixture_jpeg(4096)[:512])
    write("PngCarverFuzz", "minimal-png.bin", minimal_png())
    write("PngCarverFuzz", "signature-only.bin", b"\x89PNG\r\n\x1a\n")
    write("ZipCarverFuzz", "stored-entry.bin", stored_zip())
    write("ZipCarverFuzz", "local-header-only.bin", stored_zip()[:30])
    write("PdfCarverFuzz", "minimal-pdf.bin", minimal_pdf())
    write("PdfCarverFuzz", "header-only.bin", b"%PDF-1.4\n")
    write("Mp4CarverFuzz", "ftyp-moov-mdat.bin", minimal_mp4())
    write("Mp4CarverFuzz", "ftyp-only.bin", minimal_mp4()[:24])
    write("RawCarverFuzz", "tiff-little-endian.bin", tiff_header(big_endian=False))
    write("RawCarverFuzz", "tiff-big-endian.bin", tiff_header(big_endian=True))
    write("ByteReaderFuzz", "offset-zero.bin", byte_reader_input(0, 32))
    write("ByteReaderFuzz", "offset-past-end.bin", byte_reader_input(1 << 40, 16))
    write("ByteReaderFuzz", "offset-at-boundary.bin", byte_reader_input(16, 16))
    # The block and padding boundaries a split hash gets wrong first.
    for length in (55, 56, 64, 119, 120):
        payload = bytes(range(length % 256)) * (length // 256 + 1)
        write("Sha256Fuzz", f"len-{length}.bin", payload)
    write("NameDecodeFuzz", "ascii-utf16le.bin", "report.txt".encode("utf-16-le"))
    write("NameDecodeFuzz", "surrogate-pair.bin", "\U0001F600file".encode("utf-16-le"))
    write("NameDecodeFuzz", "lone-surrogate.bin", b"\x00\xd8\x41\x00")
    write("NameDecodeFuzz", "path-separators.bin", "a/b\\c%d".encode("utf-16-le"))
    write("OutputPathFuzz", "plain.bin", b"photo.jpg")
    write("OutputPathFuzz", "dot-dot.bin", b"../../etc/passwd")
    write("OutputPathFuzz", "absolute.bin", b"/etc/shadow")
    write("OutputPathFuzz", "windows-device.bin", b"CON:stream")
    write("OutputPathFuzz", "percent.bin", b"a%2e%2e%2fb")
    # `MountTableFuzz` is deliberately absent. Its four tracked inputs arrived
    # with story-0609 out of a fuzz run — they open with a control byte and
    # carry mangled path fragments — so they are minimized *finds*, not seeds
    # anybody authored. A generator has no business claiming to write them.
    write("SignatureScanFuzz", "one-candidate.bin", signature_scan_input(64, 32, 60))
    write("SignatureScanFuzz", "candidate-at-window-edge.bin", signature_scan_input(62, 32, 60))
    write("SignatureScanFuzz", "overlong-length.bin", signature_scan_input(0, 0xFFFF, 60))


if __name__ == "__main__":
    raise SystemExit(main())
