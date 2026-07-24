<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0002: `ImageFileDevice` (portable image reader)

- Epic: [epic-m0-foundation](../epic-m0-foundation.md)
- Status: Ready
- Size: M

## Goal

Provide the portable, privilege-free `BlockDevice` used throughout development and
testing: a read-only reader over a raw image file (`.dd`, `.img`). This is the default
source until physical-device access arrives in M4.

## Design references

- [I/O layer](../../architecture/io-layer.md)

## Acceptance criteria

- [ ] `ImageFileDevice` opens an image path **read-only** and implements `BlockDevice`.
- [ ] `sizeInBytes()` reflects the file size; `sectorSize()` defaults to 512 and is
      overridable at construction.
- [ ] `readAt` uses positioned reads (`pread` on Linux, overlapped/offset read on
      Windows) with no shared mutable file offset (thread-safe reads).
- [ ] Opening a missing/unreadable path returns a typed error, not a throw across the
      API boundary.
- [ ] A read fault returns a typed `IoError` with offset and OS error code.

## Test plan

- Integration: build a small temp image, read ranges, verify bytes match.
- Unit: missing file → typed error; tail short read; concurrent `readAt` from multiple
  threads returns correct, non-interleaved data.

## Definition of Done

- [ ] Acceptance criteria met; tests green under ASan + UBSan on Windows and Linux.
- [ ] Platform code confined to `core/io/`, selected by CMake (no scattered `#ifdef`).
- [ ] Coverage held or raised; lint/format/duplication/file-length guards clean.
- [ ] `CHANGELOG.md` updated; story-level self-audit completed.
