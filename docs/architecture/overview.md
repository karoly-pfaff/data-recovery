<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Architecture Overview

Revenant is a data-recovery toolkit built as **two thin frontends over a shared static
core** (`librevenant`). The design goal that shapes every layer is **precision**: a
recovered file is validated against its own format before it is written, so we never
emit the bloated false positives that plague magic-byte-only carvers.

## The thesis

Classic carvers (PhotoRec) work by: find a known header, then collect bytes until a
footer or the next header or a size cap. When the footer is missing or a header is
misdetected, the result is a huge, corrupt file — the canonical failure is a
multi-gigabyte "SWF" carved from a drive that only ever held photos.

Revenant instead **walks each format's internal structure** to compute its exact
length, and validates the result. If the bytes do not form a coherent file, we flag or
discard rather than write garbage. This is captured in
[ADR-0003](adr/adr-0003-validating-carving.md).

## Layered design

Each layer depends only on the layer below, through an interface. This keeps units
small, independently testable, and replaceable.

```
┌──────────────────────────────────────────────────────────────────────┐
│ CLI frontends            revenant-carve · revenant-undelete            │  cli/
│   argument parsing, config, interactive selection, progress            │
├──────────────────────────────────────────────────────────────────────┤
│ Recovery orchestration   hybrid strategy, dedup, naming, reporting     │  recovery/
├──────────────────────────────────────────────────────────────────────┤
│ Carving engine           signature registry + per-format validators    │  carve/
├──────────────────────────────────────────────────────────────────────┤
│ Filesystem parsers       NTFS · FAT32 · exFAT · ext4  (read-only)      │  fs/
├──────────────────────────────────────────────────────────────────────┤
│ Volume / partition        MBR · GPT detection                          │  volume/
├──────────────────────────────────────────────────────────────────────┤
│ Device I/O (BlockDevice)  physical · image file · logical volume       │  core/io
├──────────────────────────────────────────────────────────────────────┤
│ Core                      Result<T>, logging, byte views, endianness   │  core/
└──────────────────────────────────────────────────────────────────────┘
```

Detailed per-layer documents:

- [I/O layer](io-layer.md) — the `BlockDevice` abstraction over all three sources.
- [Filesystems](filesystems.md) — read-only parsers and undelete strategy.
- [Carving engine](carving-engine.md) — the signature registry and validating carvers.
- [Hybrid orchestration](hybrid-orchestration.md) — how undelete and carve combine.
- [Recovery output](recovery-output.md) — manifest, hashing, dry-run/preview, scaling.

## Key interfaces (seams)

The architecture is defined by a handful of interfaces. Everything above depends on
these abstractions, never on concretes (Dependency Inversion):

- **`BlockDevice`** — random-access, read-only source of sized byte ranges.
  Concrete implementations: `ImageFileDevice` and `RawDevice` (a whole disk or a
  volume, Windows/Linux), composed with the `CachingDevice` and `RetryingDevice`
  decorators and the `PartitionView` window. Tests use `InMemoryDevice`.
- **`FileSystem`** — mounts a `BlockDevice` region and enumerates live and deleted
  entries. Implementations: `NtfsFileSystem`, `Fat32FileSystem`, `ExfatFileSystem`,
  `Ext4FileSystem`.
- **`FormatCarver`** — for one file format: recognizes a header, then validates and
  measures the exact extent. Registered in the `CarverRegistry`.
- **`RecoverySink`** — receives recovered artifacts (name, bytes, metadata) and writes
  them to the destination. Implementations handle naming, dedup, and directory
  reconstruction.

## Cross-cutting concerns

- **Read-only source, always.** No layer opens the source for writing. The only writer
  is the `RecoverySink`, and it only ever writes to the destination. See
  [ADR-0005](adr/adr-0005-read-only-by-default.md).
- **Errors are values.** `Result<T>` (an `expected`-like type) propagates typed errors.
  A corrupt sector is an expected condition, not an exception path.
- **Bytes are handled safely.** `std::span<const std::byte>` views, explicit
  little/big-endian readers, and `std::bit_cast` — no unaligned dereferences, no UB.
- **Everything streams.** No layer loads an entire device into memory. Ranges are read
  on demand and scanned in bounded windows. See
  [performance strategy](../performance/strategy.md).

## Two binaries, one core

`revenant-carve` and `revenant-undelete` share `librevenant` and differ only in their
top-level strategy. `undelete` additionally invokes the carve engine over unallocated
regions in hybrid mode. Rationale in
[ADR-0002](adr/adr-0002-two-frontends-shared-core.md).

## Module map

| Directory            | Responsibility                                              |
|----------------------|-------------------------------------------------------------|
| `core/`              | `Result<T>`, logging, byte views, endian readers, buffers   |
| `core/io/`           | `BlockDevice` and its concrete devices                      |
| `volume/`            | MBR/GPT partition table parsing                             |
| `fs/`                | Filesystem parsers (one subdirectory per filesystem)        |
| `carve/`             | Carving engine, `CarverRegistry`, scanning                  |
| `carve/formats/`     | One `FormatCarver` per file format                          |
| `recovery/`          | Orchestration, hybrid strategy, candidate index + arbitration, sinks, reporting |
| `cli/`               | The two frontend executables                                |
