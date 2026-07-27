<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0008: PartitionView (MBR/GPT-free single-partition mount)

- Epic: [epic-m1-vertical-slice](../epic-m1-vertical-slice.md)
- Status: In progress
- Size: S

## Goal

Deliver the `revenant::volume` seam: a read-only `BlockDevice` that exposes a
byte-range window over another `BlockDevice`. This is the partition mount point
the NTFS filesystem (story-0009 and beyond) will consume; real MBR/GPT parsing
remains M4.

## Design references

- [BlockDevice interface](../../architecture/adr/adr-0005-blockdevice.md)

## Acceptance criteria

- [ ] `PartitionView(BlockDevice& parent, std::uint64_t start, std::uint64_t length)`
      in `include/revenant/volume/PartitionView.hpp` / `src/volume/PartitionView.cpp`.
      `length` is clamped to the parent's remaining size; `start` past the parent's
      end yields a zero-length view.
- [ ] `readAt` translates the view-relative offset by `start_`, reuses
      `clampReadRange`, and propagates parent errors. `start + offset` overflow
      produces `kOverflow`.
- [ ] `sizeInBytes()` returns the clamped view length; `sectorSize()` is a
      passthrough to the parent.

## Test plan

- Unit (`PartitionViewTest.cpp`): full read inside window; partial read at the
  window tail; read at/past the window end returns 0; `start` beyond parent end
  yields a zero-length view; explicit zero-length view; `offset` overflow is a
  typed `kOverflow` error; bytes outside the view are never returned.

## Definition of Done

- [ ] Acceptance criteria met, tests green under ASan + UBSan.
- [ ] Lint/format/duplication/file-length guards clean.
- [ ] `CHANGELOG.md` updated under `[Unreleased]/Added`.
- [ ] Epic row linked.
