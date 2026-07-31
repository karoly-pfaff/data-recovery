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
  allocated-region set (`recovery::ByteAccounting`). Overlapping and touching extents are
  fused, so the set stays proportional to distinct regions rather than to file count, and
  the carve pass is handed the complement: the gaps are the only places worth searching.
  Uncertain filesystem entries do **not** suppress carving — the region is scanned as a
  safety net. The set is capped (ADR-0009) and reports what it dropped; dropping is safe
  here in a way dropping a *candidate* would not be, because less accounting only ever
  means more scanning.
- **A region bounds the search, not the file.** A carve candidate that starts inside a
  gap is carved to its true length even when that runs past the gap's end. The boundary
  is an artifact of what some other file claimed, and truncating there would turn a whole
  recovery into an `Uncertain` fragment.
- **No filesystem is not a failure.** In hybrid mode a volume that will not mount
  downgrades the run to carving rather than ending it — a formatted or RAW volume is
  exactly what carving is for — and the run reports that it happened
  (`RecoveryStats::filesystemMounted`). In `--fs-only` the same failure *is* the result,
  and propagates as a typed error.
- **Arbitration.** Both sources append their findings to one file-backed candidate index
  (`recovery::CandidateIndex`); `arbitrate` then resolves competing explanations of the
  same bytes. A filesystem entry beats a carve of its own region **outright**, ahead of
  confidence: the two confidence scales measure different things, and a carve starting at
  a fragmented file's first run would hand back garbage however structurally perfect it
  looked. A candidate wins whole or not at all — accepting a partial overlap would emit
  exactly the fragments arbitration exists to remove.
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

- The source device is read-only throughout ([ADR-0005](adr/adr-0005-read-only-by-default.md)); the destination is separate.
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
