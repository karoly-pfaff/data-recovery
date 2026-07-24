<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Hybrid Orchestration

Hybrid mode is the "better than the sum of PhotoRec and TestDisk" behaviour: recover
what the filesystem can name, then carve everything it cannot. This is the default mode
of `revenant-undelete`.

## The strategy

```
1. Locate partitions            (volume layer: MBR/GPT)
2. For each partition:
     a. Mount the filesystem     (fs layer)
     b. Enumerate live + deleted + orphaned entries
     c. Recover confidently-graded entries WITH names/paths/timestamps
     d. Compute the "already accounted for" byte map
        (clusters belonging to confidently recovered files)
3. Carve the remaining space     (carve engine)
     - Restrict scanning to regions NOT accounted for in step 2d
     - Emit validated, carved files (no names — f0000001.jpg style)
4. Deduplicate and write         (recovery sink)
```

The core idea: filesystem recovery is **precise but fragile** (great when metadata
survives), carving is **robust but anonymous** (works on formatted/damaged volumes).
Running filesystem recovery first lets carving skip regions already recovered *with*
names, which both speeds up the scan and avoids emitting the same file twice.

Concretely, filesystem-recovered entries enter the shared **candidate index** as
high-confidence primaries, and carve candidates are resolved against them by
arbitration ([ADR-0006](adr/adr-0006-candidate-arbitration-deferred-extraction.md)):
a carve candidate on a region a named entry already covers loses and is never
extracted. Byte accounting (below) is the *performance* optimization that lets the carve
pass skip such regions; arbitration is the *correctness* authority.

## Coordinating the two sources

- **Byte accounting.** Confidently recovered files contribute their data extents to an
  allocated-region set. The carve pass consults this set and skips accounted-for ranges.
  Uncertain filesystem entries do **not** suppress carving — the region is scanned as a
  safety net.
- **Deduplication.** A carved file that is byte-identical (by content hash of the first
  and last N KiB plus length) to a named recovery is dropped in favour of the named one.
  Names are strictly better than `f0000001.jpg`.
- **Provenance.** Every emitted artifact records how it was recovered (filesystem entry
  vs. carved, and the confidence verdict) for the final report.

## Modes

`revenant-undelete` exposes three modes; `revenant-carve` is always carve-only:

| Mode              | Filesystem pass | Carve pass | Use when                                  |
|-------------------|:---------------:|:----------:|-------------------------------------------|
| `--fs-only`       | yes             | no         | Metadata intact; you want names, fast.    |
| `--hybrid` (default) | yes          | yes        | Best coverage; names where possible.      |
| `--carve-only`    | no              | yes        | Formatted/RAW volume; metadata is gone.   |

## Ordering guarantees

- The source device is read-only throughout; the destination is separate.
- Filesystem recovery and carving both stream; the orchestrator never holds a whole
  partition in memory. Progress is reported per phase and per partition.
- The orchestrator is a thin coordinator: it owns *sequencing*, not parsing. Filesystem
  and carve logic stay in their own layers (Single Responsibility).

## Configuration surface (recovery layer, not CLI)

- Which formats to carve (allowlist) — a photos-and-video recovery need not scan for
  archives, cutting false positives and time.
- Confidence threshold for emitting carved `Uncertain` results.
- Destination policy: directory reconstruction for named entries, flat buckets by
  extension for carved ones.

The CLI merely maps flags onto this surface; the policy lives in `recovery/`.
