<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Revenant

**A precise, structure-aware data-recovery toolkit.**

Revenant is a spiritual successor to PhotoRec/TestDisk that fixes their most painful
failure mode: imprecise carving. Where classic carvers find a magic byte and then
"grab bytes until the next header" — producing bloated, corrupt, false-positive files
(the infamous 1 GB "SWF" on a drive that only ever held photos) — Revenant **parses
each format's internal structure to determine its exact length**. If the bytes don't
form a valid file, we say so instead of writing garbage.

> Status: **pre-alpha / foundation.** No functional code yet — this repository
> currently contains the architecture, roadmap, and engineering standards. See
> [`docs/roadmap.md`](docs/roadmap.md).

## Two tools, one core

| Tool                  | Role                          | Analogue  |
|-----------------------|-------------------------------|-----------|
| `revenant-carve`      | Structure-aware, *validating* file carving (filesystem-independent). | PhotoRec |
| `revenant-undelete`   | Filesystem-aware recovery with original names, paths, timestamps; hybrid carving over unallocated space. | TestDisk |

Both are thin frontends over the shared static core library `librevenant`.

## Design principles

- **Precision over recall.** A carved file is validated against its own format. We
  prefer a smaller set of *correct* files over a large pile of plausible garbage.
- **Discover, then decide.** Matches are indexed as *candidates*, not extracted on
  sight. Overlapping candidates compete by confidence; a weak secondary match is written
  only if nothing better covers its region — so spurious hits never crowd out real files.
- **Read-only by default.** The source device is never modified. Recovered data is
  written to a separate destination.
- **Cross-platform.** Windows and Linux, over physical devices, disk images, and
  logical volumes — behind a single `BlockDevice` abstraction.
- **Extensible carving.** Adding a new format is a small, guided recipe
  (`revenant:add-format-carver`): a validating parser plus its mandatory tests.

## Planned coverage

- **Filesystems (undelete):** NTFS, FAT32, exFAT, ext4.
- **Formats (carve):** images (JPEG, PNG, HEIC, RAW: CR2/NEF/ARW, GIF, TIFF, WebP),
  video (MP4/MOV, AVI, MKV), documents (PDF, DOCX/XLSX/PPTX, legacy DOC/XLS),
  archives (ZIP, RAR, 7z).

## Build

Requires a C++20 compiler (MSVC 2022 / GCC 13+ / Clang 16+), CMake ≥ 3.25, and vcpkg.
Full setup instructions: [`docs/install.md`](docs/install.md).

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

See [`CLAUDE.md`](CLAUDE.md) for the full command set.

## Documentation

- [Architecture overview](docs/architecture/overview.md)
- [Roadmap](docs/roadmap.md) · [Backlog](docs/backlog/README.md)
- [Testing strategy](docs/testing/strategy.md) · [Quality gates](docs/testing/quality-gates.md)
- [Code quality standard](docs/code-quality.md) · [Versioning](docs/versioning.md) · [Git workflow](docs/git-workflow.md)
- [Performance strategy](docs/performance/strategy.md) · [Recovery output & modes](docs/architecture/recovery-output.md)
- [Development setup](docs/install.md) · [Contributing](docs/contributing.md) · [Glossary](docs/glossary.md) · [Security](SECURITY.md)

## Engineering standards

This project holds itself to enterprise-grade standards enforced in CI. The binding
contract is **[`AGENTS.md`](AGENTS.md)**. In short: one function does one thing; no
file over 250 lines; every parser is fuzzed; nothing merges without passing format,
lint, sanitizer, duplication, and coverage gates.

## License

GPL-3.0-or-later. See [`LICENSE`](LICENSE).
