<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0102: NTFS boot sector + `$MFT` locator

- Epic: [epic-m1-vertical-slice](../epic-m1-vertical-slice.md)
- Status: Done
- Size: M

## Goal

Parse and validate the 512-byte NTFS boot sector, producing the geometry needed
to locate the `$MFT` and the MFT record size. This is the first filesystem-level
step in the M1 NTFS undelete vertical slice.

## Design references

- The M1 Plan B NTFS vertical-slice plan (2026-07-26) — a working document kept
  outside this repository; its decisions live in the ADRs and this story.

## Acceptance criteria

- [x] `NtfsGeometry` in `include/revenant/fs/ntfs/BootSector.hpp` exposes
      `bytesPerSector`, `bytesPerCluster`, `totalClusters`, `mftOffsetBytes`,
      and `bytesPerMftRecord`.
- [x] `parseBootSector(std::span<const std::byte>)` validates the on-disk table:
      OEM ID `"NTFS    "`, bytes-per-sector in `{512,1024,2048,4096}`,
      power-of-two `sectorsPerCluster <= 128`, `totalSectors > 0`,
      `mftClusterNumber < totalClusters`, `clustersPerMftRecord` producing a
      record size in `[256, 65536]`, and boot signature `55 AA`.
- [x] Truncated input returns `kOutOfRange`; any rule violation returns
      `kInvalidArgument` with the field's byte offset.
- [x] A libFuzzer target `NtfsBootSectorFuzz` is wired and must never crash.

## Test plan

- Unit (`BootSectorTest.cpp`): hand-built valid sector (bps 512, spc 8,
  total 16384, mft cluster 4, cpm `0xF6` -> 1024 bytes/record); truncated
  input; each validation row violated one at a time; positive
  `clustersPerMftRecord` path; both signature bytes checked.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] Lint/format/duplication/file-length guards clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]/Added`.
- [x] Epic row linked.
