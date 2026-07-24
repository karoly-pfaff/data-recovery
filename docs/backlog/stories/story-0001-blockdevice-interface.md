<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0001: `BlockDevice` interface + `InMemoryDevice`

- Epic: [epic-m0-foundation](../epic-m0-foundation.md)
- Status: Ready
- Size: M

## Goal

Establish the single seam through which all byte sources are read. Define the
`BlockDevice` interface and provide `InMemoryDevice`, the in-memory implementation that
backs the majority of unit tests. Everything above the I/O layer depends on this
abstraction, never on a concrete device.

## Design references

- [I/O layer](../../architecture/io-layer.md)
- [ADR-0005: read-only by default](../../architecture/adr/adr-0005-read-only-by-default.md)

## Acceptance criteria

- [ ] `BlockDevice` is declared in `include/revenant/core/io/BlockDevice.hpp` with
      `sizeInBytes()`, `sectorSize()`, and `readAt(offset, span) -> Result<size_t>`.
- [ ] The interface exposes no write operation.
- [ ] `InMemoryDevice` (in `tests/`) is constructed from a byte buffer and a configurable
      sector size, and implements `BlockDevice`.
- [ ] `readAt` beyond end-of-device returns a short read (value), not an error.
- [ ] `readAt` with a zero-size span returns 0 and reads nothing.

## Test plan

- Unit: full read; partial read at the tail; read entirely past the end (0 bytes);
  zero-length read; offset + size overflow is handled as a typed error, not UB.
- Unit: `sectorSize()` is reported as constructed.
- The interface is used through a `BlockDevice&` in tests to confirm it is a usable seam.

## Definition of Done

- [ ] Acceptance criteria met; tests green under ASan + UBSan on Windows and Linux.
- [ ] Coverage held or raised (≥ 85% core).
- [ ] clang-format, clang-tidy, duplication, and file-length guard clean.
- [ ] `CHANGELOG.md` updated under `[Unreleased]`.
- [ ] Story-level self-audit checklist ([code-quality](../../code-quality.md)) completed.
- [ ] Each file ≤ 250 lines; each function ≤ 10 statements, does one thing.
