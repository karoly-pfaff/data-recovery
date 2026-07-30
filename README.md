<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Revenant

**A precise, structure-aware data-recovery toolkit.**

Revenant is a spiritual successor to PhotoRec/TestDisk that fixes their most painful
failure mode: imprecise carving. Where classic carvers find a magic byte and then
"grab bytes until the next header" — producing bloated, corrupt, false-positive files
(the infamous 1 GB "SWF" on a drive that only ever held photos) — Revenant **parses
each format's internal structure to determine its exact length**. If the bytes don't
form a valid file, we say so instead of writing garbage.

> Status: **pre-alpha.** Both binaries work over disk images *and* real devices.
> `revenant-undelete` recovers NTFS, FAT32, exFAT and ext4 volumes end to end —
> named files by their metadata, the rest by validating carve — and reads a whole
> partitioned disk, MBR or GPT, walking every volume on it. `revenant-carve`
> recovers by structure alone. What has shipped and what is still ahead is tracked
> in [`docs/roadmap.md`](docs/roadmap.md).

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
- **The source is never written.** Its handle is opened without write access, and no
  layer above the I/O boundary can express a write to it. Recovered data goes to a
  separate destination. See
  [ADR-0005](docs/architecture/adr/adr-0005-read-only-by-default.md) and
  [ADR-0011](docs/architecture/adr/adr-0011-two-halves-of-the-read-only-guarantee.md).
- **Cross-platform.** Windows and Linux, over physical devices, disk images, and
  logical volumes — behind a single `BlockDevice` abstraction.
- **Extensible carving.** Adding a new format is a small, guided recipe
  (the `add-format-carver` skill): a validating parser plus its mandatory tests.

## Planned coverage

- **Filesystems (undelete):** NTFS, FAT32, exFAT, ext4.
- **Formats (carve):** images (JPEG, PNG, HEIC, RAW: CR2/NEF/ARW, GIF, TIFF, WebP),
  video (MP4/MOV, AVI, MKV), documents (PDF, DOCX/XLSX/PPTX, legacy DOC/XLS),
  archives (ZIP, RAR, 7z).

## Build

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

Prerequisites, the pinned tool versions, the platform caveats and every other command:
[`docs/install.md`](docs/install.md).

## Usage

Point either binary at a disk image or a real device and give it a destination to write
to. `revenant-undelete` recovers named files from the filesystem and carves what the
names did not account for; `revenant-carve` carves alone. Both list partitions, resume
after `Ctrl-C`, and never write to the source.

The command-line reference — every flag, whole disks, output layout, sessions and dry
runs — is [`docs/usage.md`](docs/usage.md).

## Documentation

**Using it** — [Usage reference](docs/usage.md) · [Recovery output & modes](docs/architecture/recovery-output.md) · [Changelog](CHANGELOG.md)

**Building it** — [Development setup](docs/install.md) · [Contributing](CONTRIBUTING.md) · [Git workflow](docs/git-workflow.md) · [Versioning & releases](docs/versioning.md)

**How it works** — [Architecture overview](docs/architecture/overview.md) · [Decision records](docs/architecture/adr/README.md) · [Performance strategy](docs/performance/strategy.md) · [Glossary](docs/glossary.md)

**How it is held to account** — [Engineering contract](AGENTS.md) · [Code quality standard](docs/code-quality.md) · [Testing strategy](docs/testing/strategy.md) · [Quality gates](docs/testing/quality-gates.md) · [Security policy](SECURITY.md)

**Where it is going** — [Roadmap](docs/roadmap.md) · [Backlog](docs/backlog/README.md)

## Engineering standards

The binding contract is **[`AGENTS.md`](AGENTS.md)**: one function does one thing, at
one level of abstraction. What can be checked by a machine is — see
[quality gates](docs/testing/quality-gates.md). The rest is checked by the
[self-audit](docs/code-quality.md) every story runs before it is Done.

## License

GPL-3.0-or-later. See [`LICENSE`](LICENSE).
