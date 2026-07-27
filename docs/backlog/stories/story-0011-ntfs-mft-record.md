<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0011: NTFS MFT record + attribute parser (`$STANDARD_INFORMATION`, `$FILE_NAME`)

- Epic: [epic-m1-vertical-slice](../epic-m1-vertical-slice.md)
- Status: Done
- Size: L

## Goal

Parse a single NTFS MFT record: validate the `FILE` signature, apply the
update-sequence-array (USA) fixup, iterate resident attributes, and extract the
three core attribute types needed for filesystem-level recovery:
`$STANDARD_INFORMATION`, `$FILE_NAME`, and resident `$DATA`.

## Design references

- [M1 Plan B NTFS vertical slice](../../../../superpowers/plans/2026-07-26-m1-plan-b-ntfs.md)

## Acceptance criteria

- [x] `MftRecordView` in `include/revenant/fs/ntfs/MftRecord.hpp` exposes
      `recordNumber`, `inUse`, `isDirectory`, `sequence`, `grade`, `fixedUp`,
      `standardInfo`, `names`, and `data`.
- [x] `parseMftRecord(std::span<const std::byte>, std::uint64_t)` validates the
      `FILE` record signature and header bounds, returning `kNotFound` for an
      invalid signature and `kInvalidArgument` for a malformed header.
- [x] Update-sequence fixup is applied: the USA is read at offset `0x04/0x06`,
      bounds are checked, and the last two bytes of every 512-byte stride are
      restored. A torn fixup (USN mismatch) marks the record `kUncertain`.
- [x] Resident attribute headers are parsed, including type, length,
      resident/non-resident flag, name length/offset, content offset, and
      content length. Invalid lengths/offsets return `kInvalidArgument` and stop
      attribute parsing at `kUncertain`.
- [x] `$STANDARD_INFORMATION` content is parsed into `Timestamps`
      (`created`, `modified`, `accessed`).
- [x] `$FILE_NAME` content is parsed into `MftFileName`: parent record/sequence,
      real size, namespace, and the filename decoded through the existing
      `NameDecode` path.
- [x] Resident `$DATA` content is captured as `MftData::residentContent`.
      Non-resident `$DATA` is captured as `MftData::runlistBytes` for the
      runlist decoder (story-0012).
- [x] Extension records (`baseRecord != 0`) are returned as an `kUncertain`
      shell with no parsed attributes.
- [x] A libFuzzer target `MftRecordFuzz` is wired and must never crash.

## Test plan

- Unit (`MftRecordTest.cpp`): a synthetic 1024-byte record produced by
  `MftRecordTestSupport` with valid header, USA, and all three attributes.
- Positive cases: header fields parsed; standard-info timestamps; filename
  fields and UTF-8 text; resident data content; in-use/directory flags.
- Negative/edge cases: bad signature; torn fixup; base record non-zero
  (extension); over-declared attribute length; filename name-length overrun
  (drops name, keeps record); unknown attribute type skipped; deleted flag
  read correctly.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] Lint/format/duplication/file-length guards clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]/Added`.
- [x] Epic row linked.
