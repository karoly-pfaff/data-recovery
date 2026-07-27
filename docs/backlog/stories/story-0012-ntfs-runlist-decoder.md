<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0012: NTFS `$DATA` runlist decoder (resident + non-resident)

- Epic: [epic-m1-vertical-slice](../epic-m1-vertical-slice.md)
- Status: Done
- Size: M

## Goal

Turn the non-resident `$DATA` runlist bytes captured by
[story-0011](story-0011-ntfs-mft-record.md) into the device-relative byte extents
that hold a file's content, so `revenant-undelete` can read a deleted file's data
rather than only its metadata.

## Design references

- [Filesystem layer](../../architecture/filesystems.md) — "MFT record parsing,
  attribute parsing, and runlist decoding do not live in one function".
- [ADR-0009](../../architecture/adr/adr-0009-output-safety.md) — bounded allocation:
  no on-disk count sizes an allocation or a loop unchecked.

## Scope

Two responsibilities behind one public header
`include/revenant/fs/ntfs/Runlist.hpp`:

1. **Decoding** (`decodeRunlist`) — structural only, no geometry. Walks the
   NTFS data-run encoding (header nibble pair, unsigned length, *signed* LCN
   delta) and yields cluster-space runs. Split across `Runlist.cpp` (the walk
   and cluster placement) and `RunlistRun.cpp` (reading one run out of the
   bytes), which meet at the internal `RawRun` seam.
2. **Mapping** (`runlistExtents`, `RunlistExtents.cpp`) — geometry-aware.
   Converts cluster-space runs into `fs::Extent` byte ranges against a
   validated `NtfsGeometry`, and trims the tail to the attribute's declared
   real size.

Resident `$DATA` is already carried as bytes by `MftData::residentContent` and
needs no decoding; the mapper is only ever asked about non-resident data.

**Out of scope (deliberate):** sparse and compressed `$DATA`. `decodeRunlist`
records a sparse run faithfully (`DataRun::sparse`); `runlistExtents` refuses a
runlist containing one with a typed `kInvalidArgument`, so the hybrid
orchestrator routes that file to the carve pass instead of silently emitting
wrong bytes. Widening this is a later story, not a hidden fallback here.

## Acceptance criteria

- [x] `include/revenant/fs/ntfs/Runlist.hpp` exposes `DataRun`
      (`startCluster`, `lengthClusters`, `sparse`), `Runlist` (`runs`,
      `totalClusters`), `decodeRunlist`, and `runlistExtents`.
- [x] `decodeRunlist(std::span<const std::byte>)` walks data runs until the
      `0x00` end marker and returns the decoded runs plus their cluster total.
- [x] The run header's low nibble is the length-field width and the high nibble
      the offset-field width. A zero length-field width, a width above 8, or a
      field that runs past the end of the runlist bytes is a typed error, not a
      truncated read.
- [x] The run length is read little-endian unsigned; a zero-cluster run is
      rejected (`kInvalidArgument`).
- [x] The run offset is read little-endian **signed** and applied as a delta to
      the previous run's starting LCN. A delta that would make the LCN negative,
      or that wraps 64-bit, is rejected (`kInvalidArgument` / `kOverflow`).
- [x] An offset-field width of zero marks the run `sparse`; no LCN is derived
      and the previous LCN carries forward unchanged.
- [x] A runlist with no `0x00` terminator inside its bytes is rejected
      (`kOutOfRange`) — the decoder never reads past the span.
- [x] The run count is bounded by a named constant (`kMaxDataRuns`); exceeding
      it is `kOutOfRange`, per ADR-0009.
- [x] Accumulating `totalClusters` is overflow-checked (`kOverflow`).
- [x] `runlistExtents(const Runlist&, const NtfsGeometry&, std::uint64_t realSize)`
      returns `std::vector<Extent>` with `deviceOffset = startCluster *
      bytesPerCluster` and `lengthBytes = lengthClusters * bytesPerCluster`.
- [x] A run that ends beyond `geometry.totalClusters` is rejected
      (`kInvalidArgument`) — a runlist may not point outside the volume.
- [x] The final extent is trimmed so the extents sum to `realSize`; a `realSize`
      larger than the allocated clusters is rejected (`kInvalidArgument`).
- [x] A libFuzzer target `RunlistFuzz` is wired and must never crash.

## Test plan

Unit (`tests/unit/fs/ntfs/RunlistTest.cpp`, `RunlistExtentsTest.cpp`):

- Positive: single run; multiple runs with positive deltas; a **negative** delta
  moving the LCN backwards; a fragmented three-run list; a sparse run between two
  allocated runs; the canonical `0x21 0x18 0x34 0x56 0x00` textbook encoding.
- Negative/edge: empty span; missing terminator; length width 0; width > 8;
  field truncated by the end of the span; zero-length run; delta driving the LCN
  below zero; delta overflowing the LCN; cluster-total overflow; run count at and
  over `kMaxDataRuns`.
- Mapper: byte offsets/lengths against a known `bytesPerCluster`; tail trimmed to
  `realSize`; run past `totalClusters` rejected; `realSize` exceeding the
  allocation rejected; sparse run rejected with `kInvalidArgument`.

Fuzz (`tests/fuzz/RunlistFuzz.cpp`): arbitrary bytes into `decodeRunlist`, then
`runlistExtents` over any successful decode with a fixed synthetic geometry.
Seed corpus: the canonical encoding plus one sparse case.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] Coverage held or raised (>= 85% core) — enforced by the CI coverage job;
      the instrumentation is GCC/Clang-only, so it is not reproducible on the
      local MSVC build.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Epic row linked.
