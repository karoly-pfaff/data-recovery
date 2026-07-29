<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0101: PartitionView (MBR/GPT-free single-partition mount)

- Epic: [epic-m1-vertical-slice](../epic-m1-vertical-slice.md)
- Status: Done
- Size: S

## Goal

Deliver the `revenant::volume` seam: a read-only `BlockDevice` that exposes a
byte-range window over another `BlockDevice`. This is the partition mount point
the NTFS filesystem (story-0102 and beyond) will consume; real MBR/GPT parsing
remains M4.

## Design references

- [BlockDevice interface](../../architecture/adr/adr-0005-blockdevice.md)

## Acceptance criteria

- [x] `PartitionView(BlockDevice& parent, std::uint64_t start, std::uint64_t length)`
      in `include/revenant/volume/PartitionView.hpp` / `src/volume/PartitionView.cpp`.
      `length` is clamped to the parent's remaining size; `start` past the parent's
      end yields a zero-length view.
- [x] `readAt` translates the view-relative offset by `start_`, reuses
      `clampReadRange`, and propagates parent errors. `start + offset` overflow
      produces `kOverflow`.
- [x] `sizeInBytes()` returns the clamped view length; `sectorSize()` is a
      passthrough to the parent.

## Test plan

- Unit (`PartitionViewTest.cpp`): full read inside window; partial read at the
  window tail; read at/past the window end returns 0; `start` beyond parent end
  yields a zero-length view; explicit zero-length view; `offset` overflow is a
  typed `kOverflow` error; bytes outside the view are never returned.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] Lint/format/duplication/file-length guards clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]/Added`.
- [x] Epic row linked.
