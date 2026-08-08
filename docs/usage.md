<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Using Revenant

The command-line reference for both binaries. What the project is and why is in
[`README.md`](../README.md); how to build it is in [install.md](install.md); why the
output is shaped the way it is, in
[recovery output & modes](architecture/recovery-output.md).

## revenant-undelete

```bash
revenant-undelete --source <image> --destination <directory> \
                  [--hybrid | --fs-only | --carve-only] \
                  [--session <directory>] [--dry-run] [--partition <n>]
```

The source is opened read-only and is never written to
([ADR-0005](architecture/adr/adr-0005-read-only-by-default.md)); recovered files go to
the destination, which must exist, be a directory, and not contain the source.

| Mode           | What it does                                                    |
|----------------|-----------------------------------------------------------------|
| `--hybrid`     | Default. Recovers named files from the filesystem, then carves whatever those names did not account for. |
| `--fs-only`    | Metadata only. Fast, and every recovered file keeps its name and path. |
| `--carve-only` | Ignores the filesystem entirely — the mode a formatted or RAW volume needs. |

## Flags both tools take

| Flag | What it does |
|------|--------------|
| `--source <path>` | The image file or device to recover from. Required. |
| `--destination <dir>` | Where recovered files are written. Required, except with `--list-partitions`. |
| `--session <dir>` | Where the run's resumable state lives. Defaults to `<destination>/.revenant`. |
| `--dry-run` | Scan and report, but write no recovered files. The manifest still records every artifact. |
| `--list-partitions` | List the partitions on the source and exit. |
| `--partition <n>` | Recover only partition `<n>`, numbered from 1. |
| `--force-portable` | Disable the CPU-specific scanner fast path. Diagnostic; the portable and accelerated scanners are required to agree. |
| `--allow-unverified-destination` | Proceed when the source's physical identity cannot be resolved — a VeraCrypt volume has none, and without this the run is refused whatever the destination. It says *you* checked the destination is not on the source. It does **not** relax a proven overlap, or a destination whose tree would grow around the source: neither is a question anyone was asked. The manifest records that the run started on an unverified identity. |
| `--help` | Print the usage and exit. Accepted anywhere on the command line. |

**This table is not the authority — `--help` is.** Both binaries render their flag list
from the same descriptor table their parser dispatches on
([story-0702](backlog/stories/story-0702-one-flag-table.md)), so `--help` cannot name a
flag the parser refuses, or omit one it accepts. If this page and `--help` ever disagree,
`--help` is right and this page is stale.

## revenant-carve

When there is no filesystem left to read at all, reach for the carver directly:

```bash
revenant-carve --source <image> --destination <directory> \
               [--formats jpg,png] \
               [--session <directory>] [--dry-run] [--partition <n>]
```

`revenant-carve` is always carve-only — it has no mode flag — and `--formats` narrows
the scan at registration, so an excluded format costs nothing: no signature search, no
carve attempt. `revenant-carve --help` lists the format names this build accepts.

## Whole disks

`--source` takes a real device as readily as an image: `\\.\PhysicalDrive0` or `\\.\C:`
on Windows, `/dev/sda` or `/dev/sda1` on Linux. Both are opened read-only, and both need
administrator (Windows) or root/`disk`-group (Linux) privilege — the tool says so plainly
if it does not have it.

A whole disk carries a partition table. Either binary will read it — MBR or GPT, and a
GPT whose primary header is damaged is answered from its backup copy in the last sector:

```bash
revenant-undelete --source /path/to/disk.img --list-partitions
```

```
partitions: GPT, 2 found
  1: offset 1048576, length 536870912, System
  2: offset 537919488, length 1073741824, Data
```

Listing writes nothing and needs no destination. Pass `--partition <n>` to run the
recovery inside one of them; the number is the one the listing printed.

## What a run leaves behind

Named entries are rebuilt at their original paths; carved ones land in `carved/<ext>/`,
numbered in device order. A run's candidate index and its `manifest.json` — provenance,
source extents, and a SHA-256 per artifact — are written to `<destination>/.revenant`
unless `--session` points somewhere else.

`--dry-run` does everything but the writing: it scans, arbitrates, and emits the manifest
of what *would* come back, leaving the destination untouched.

## Interrupting a run

**Interrupting is safe.** `Ctrl-C` finishes the chunk it is on, records how far the scan
got, and stops; re-running the same command into the same destination carries on from
there. An interrupted run deliberately writes nothing — a partial scan can pick winners a
finished one would have thrown away — so it exits non-zero and says the scan is
incomplete. See
[ADR-0008](architecture/adr/adr-0008-resumability-checkpointing.md).
