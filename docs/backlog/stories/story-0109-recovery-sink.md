<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0109: `RecoverySink` — naming, destination validation, extraction

- Epic: [epic-m1-vertical-slice](../epic-m1-vertical-slice.md)
- Status: Done
- Size: M

## Goal

Turn arbitration's winners into files on disk. Everything up to here has been
discovery: [story-0112](story-0112-candidate-index-arbitration.md) decided *what*
should come back, and this story is the one place that writes it — the first
code in the project allowed to create anything at all.

## Design references

- [ADR-0006](../../architecture/adr/adr-0006-candidate-arbitration-deferred-extraction.md) —
  only winners are materialized; extraction happens after arbitration, never
  during discovery.
- [ADR-0009](../../architecture/adr/adr-0009-output-safety.md) — every output
  path goes through `sanitizeOutputPath`; there is no other way to derive one.
- [ADR-0010](../../architecture/adr/adr-0010-filename-decoding-safe-output.md) —
  decoded names, and deterministic collision suffixing via `disambiguate`.
- [ADR-0005](../../architecture/adr/adr-0005-read-only-by-default.md) — the
  source is read; the destination is somewhere else, and the sink refuses to
  write into the source's own directory.
- [Recovery output](../../architecture/recovery-output.md) — named entries
  reconstruct their tree, carved ones go into type buckets.

## Scope

1. **Naming** — `outputNameFor`. A named entry keeps its volume-relative path;
   a carved winner becomes `carved/<ext>/f0000001.<ext>`, numbered in device
   order so a run is reproducible. Both then pass through
   `sanitizeOutputPath`, which is the only way a name may reach the filesystem.
2. **The sink** — `RecoverySink`. Validates the destination once, then writes
   each winner: resident bytes as parsed, non-resident bytes read back through
   the winner's extents from the source device.
3. **Collision handling** — two winners wanting one path is resolved by
   `disambiguate` (story-0114), against the names this run has already used.

## Design decisions

**Content-hash de-duplication moves to [story-0115](../epic-m1-vertical-slice.md).**
The epic listed it here, but after arbitration a carve overlapping a named
entry is already gone, so what would remain is the rarer case of the same bytes
appearing twice in different places. Story-0062 computes a SHA-256 per artifact
for the manifest regardless; de-duplicating off that real hash is strictly
better than a bespoke boundary comparison here, and building both would be the
duplication the contract forbids. What this story *does* deduplicate is the
thing that actually bites every run: two winners wanting the same output name.

**A short write is a failure, not a smaller file.** Every extraction either
lands whole or reports a typed error and leaves the artifact behind as evidence
of what was attempted, counted in the sink's stats. A recovery tool that
quietly writes truncated files is worse than one that stops.

**Reads are bounded per extent.** A winner's extents come from on-disk
metadata, so the sink copies in bounded chunks rather than allocating a whole
file's worth of buffer (ADR-0009). A 4 GiB video does not become a 4 GiB
allocation.

## Acceptance criteria

- [x] `outputNameFor(candidate, ordinal)` returns a named entry's own path, and
      `carved/<ext>/f<8-digit ordinal>.<ext>` for a carved one.
- [x] A carved candidate whose extension is empty is bucketed as `bin`.
- [x] `RecoverySink::open(destination)` refuses a destination that does not
      exist, is not a directory, or contains the source path (ADR-0005).
- [x] `extract(winners, device)` writes every winner and returns
      `ExtractionStats` — written, bytes, failed, renamed.
- [x] A named entry's directory tree is reconstructed under the destination.
- [x] Resident content is written from the candidate itself, without going back
      to the device.
- [x] Non-resident content is read through the winner's extents, in order, and
      the file is byte-identical to what was on the device.
- [x] A path that `sanitizeOutputPath` refuses is a counted failure, not a
      write outside the destination.
- [x] Two winners wanting one path are disambiguated, and the rename is
      counted so it is visible rather than silent.
- [x] A winner whose extents cannot be read fully is a counted failure.
- [x] Nothing outside the destination is ever created.

## Test plan

Unit (`tests/unit/recovery/OutputNameTest.cpp`): a named entry keeps its path;
a carved jpg becomes `carved/jpg/f00000001.jpg`; the ordinal is zero-padded and
increases; an empty extension buckets to `bin`; an extension with a path
separator in it cannot escape the bucket.

Unit (`tests/unit/recovery/RecoverySinkTest.cpp`): a missing destination, a
file-as-destination, and a destination containing the source are all refused; a
resident winner is written from its own bytes; a fragmented winner is read
through its extents and matches; two winners with one name produce two files
and one counted rename; a traversal-shaped name is refused and counted; a
winner pointing past the device end is counted as failed and no partial file is
left claiming to be whole.

Integration (`tests/integration/RecoveredFilesTest.cpp`): the whole pipeline
over the story-0118 fixture image — hybrid run, index, arbitrate, extract — and
then every written file is compared byte-for-byte against the fixture table.
`photos/deleted.jpg` comes back at its path with its bytes, and the JPEG no
record points at comes back under `carved/jpg/`.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] Coverage held or raised (>= 85% core) — enforced by the CI coverage job;
      the instrumentation is GCC/Clang-only, so it is not reproducible on the
      local MSVC build.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` + `docs/architecture/recovery-output.md` updated.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Epic row linked.
