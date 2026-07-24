<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# ADR-0006: Candidate arbitration & deferred extraction

- **Status:** Accepted
- **Date:** 2026-07-24

## Context

PhotoRec extracts eagerly: the moment a signature matches and something is carved, it is
written to disk. On a damaged drive this produces piles of low-quality output — including
spurious secondary matches (the canonical multi-gigabyte "SWF" on a photos-and-video
drive) written right alongside the real files, because the carver never asks "is there a
better explanation for these same bytes?"

Multiple carvers (and the filesystem layer) frequently produce **overlapping candidates**
for the same region: a strong, high-confidence primary match and one or more weak,
low-confidence secondary matches. Eager extraction materializes all of them.

## Decision

Separate **discovery** from **extraction** with a candidate arbitration stage backed by a
**file-based candidate index** (not in-memory — this must scale to terabyte devices).

1. **Discover.** Scanning + validation produces *candidates*, not files. Each candidate
   records `{region (offset, length), format, confidence, source}` and is appended to a
   persistent, file-backed candidate index. Nothing is extracted yet.
2. **Arbitrate.** Candidates competing for overlapping regions are resolved by
   confidence: a higher-confidence candidate suppresses lower-confidence ones covering
   the same region. Filesystem-recovered entries (named, high confidence) enter the index
   as primaries and therefore win over carve candidates on their regions automatically.
3. **Extract.** Only winning candidates are materialized to the destination. A weak
   secondary match is written **only if no primary candidate covers its region** — the
   behaviour the SWF example calls for.

Confidence-tie and partial-overlap rules, and whether an `Uncertain` orphan region is
emitted at all, are policy in the `recovery/` layer (configurable), not hard-coded in
carvers. A carver's job ends at returning a verdict; it never forces extraction.

## Consequences

- Dramatically fewer false positives: secondary matches are indexed and discarded when a
  better explanation exists, instead of cluttering the output.
- The engine gains a persistent candidate index and a distinct arbitration phase; the
  carve/filesystem layers stay unchanged in responsibility (they only produce
  candidates). This reinforces the [validating-carving](adr-0003-validating-carving.md)
  and [hybrid](../hybrid-orchestration.md) designs rather than complicating them.
- Extraction is deferred until arbitration completes for a region, so peak output is the
  set of winners, not every hypothesis.
- The candidate index is itself parsed/scanned data structure state; it is covered by the
  same testing rigor (including malformed-index handling) as the rest of the engine.
- Fragmentation-aware reassembly, if ever added, fits naturally as a later arbitration
  input and remains out of scope for now (YAGNI).
