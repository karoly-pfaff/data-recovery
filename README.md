<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Revenant

**A precise, structure-aware data-recovery toolkit.**

Revenant is a spiritual successor to PhotoRec/TestDisk that fixes their most painful
failure mode: imprecise carving. Where classic carvers find a magic byte and then
"grab bytes until the next header" — producing bloated, corrupt, false-positive files
(the infamous 1 GB "SWF" on a drive that only ever held photos) — Revenant **parses
each format's internal structure to determine its exact length**. If the bytes don't
form a valid file, we say so instead of writing garbage.

> Status: **pre-alpha.** Both binaries work over disk images: `revenant-undelete`
> recovers NTFS volumes end to end — named files by their metadata, the rest by
> validating carve — and `revenant-carve` recovers by structure alone. Breadth
> (more filesystems, physical devices) is still ahead. See
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

## Usage

```bash
revenant-undelete --source <image> --destination <directory> \
                  [--hybrid | --fs-only | --carve-only] \
                  [--session <directory>] [--dry-run] [--partition <n>]
```

The source is opened read-only and is never written to; recovered files go to the
destination, which must exist, be a directory, and not contain the source.

| Mode           | What it does                                                    |
|----------------|-----------------------------------------------------------------|
| `--hybrid`     | Default. Recovers named files from the filesystem, then carves whatever those names did not account for. |
| `--fs-only`    | Metadata only. Fast, and every recovered file keeps its name and path. |
| `--carve-only` | Ignores the filesystem entirely — the mode a formatted or RAW volume needs. |

When there is no filesystem left to read at all, reach for the carver directly:

```bash
revenant-carve --source <image> --destination <directory> \
               [--formats jpg,png] \
               [--session <directory>] [--dry-run] [--partition <n>]
```

`revenant-carve` is always carve-only — it has no mode flag — and `--formats`
narrows the scan at registration, so an excluded format costs nothing: no
signature search, no carve attempt. `revenant-carve --help` lists the format
names this build accepts.

### Whole disks

A source that is a whole disk rather than a single volume carries a partition
table. Either binary will read it — MBR or GPT, and a GPT whose primary header
is damaged is answered from its backup copy in the last sector:

```bash
revenant-undelete --source /path/to/disk.img --list-partitions
```

```
partitions: GPT, 2 found
  1: offset 1048576, length 536870912, System
  2: offset 537919488, length 1073741824, Data
```

Listing writes nothing and needs no destination. Pass `--partition <n>` to run
the recovery inside one of them; the number is the one the listing printed.

Named entries are rebuilt at their original paths; carved ones land in
`carved/<ext>/`, numbered in device order. A run's candidate index and its
`manifest.json` — provenance, source extents, and a SHA-256 per artifact — are
written to `<destination>/.revenant` unless `--session` points somewhere else.

`--dry-run` does everything but the writing: it scans, arbitrates, and emits the
manifest of what *would* come back, leaving the destination untouched. See
[recovery output & modes](docs/architecture/recovery-output.md).

**Interrupting is safe.** `Ctrl-C` finishes the chunk it is on, records how far
the scan got, and stops; re-running the same command into the same destination
carries on from there. An interrupted run deliberately writes nothing — a partial
scan can pick winners a finished one would have thrown away — so it exits
non-zero and says the scan is incomplete. See
[ADR-0008](docs/architecture/adr/adr-0008-resumability-checkpointing.md).

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
