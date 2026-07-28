<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Epic M1 — Vertical slice

**Goal:** the end-to-end proof. Recover deleted JPEGs from a synthetic NTFS image both
by name (filesystem) and by carving, combined in one hybrid run — exercising every
architectural layer exactly once, on a narrow but real slice.

**Milestone:** [M1](../roadmap.md#m1--vertical-slice)

## Outcome / definition of ready-to-close

- Given a crafted NTFS image containing deleted JPEG files, `revenant-undelete --hybrid`
  recovers them with original names/paths where metadata survives, and carves the rest.
- `revenant-carve` recovers the same JPEGs by carving alone.
- Carved JPEGs are exact (validated SOI→EOI extents), verified by golden-file tests —
  no oversized false positives.
- First tagged pre-release (`v0.1.0`).

## Stories

| Story | Title | Size |
|-------|-------|:----:|
| story-0008 | see [story-0008](stories/story-0008-partition-view.md): MBR/GPT-free single-partition mount for the test image | S |
| story-0009 | see [story-0009](stories/story-0009-ntfs-boot-sector.md): NTFS boot sector + `$MFT` locator | M |
| story-0010 → | see [story-0010](stories/story-0010-jpeg-validating-carver.md): JPEG validating carver | M |
| story-0011 | see [story-0011](stories/story-0011-ntfs-mft-record.md): NTFS MFT record + attribute parser (`$STANDARD_INFORMATION`, `$FILE_NAME`) | L |
| story-0012 → | see [story-0012](stories/story-0012-ntfs-runlist-decoder.md): NTFS `$DATA` runlist decoder (resident + non-resident) | M |
| story-0065 → | see [story-0065](stories/story-0065-ntfs-image-builder.md): NTFS synthetic-image builder in tools/imagegen | L |
| story-0013 → | see [story-0013](stories/story-0013-ntfs-entry-enumeration.md): NTFS deleted-entry enumeration + path reconstruction | M |
| story-0014 → | see [story-0014](stories/story-0014-carver-registry-and-scan.md): `CarverRegistry` + streaming signature scan | M |
| story-0015 → | see [story-0015](stories/story-0015-hybrid-orchestrator.md): Hybrid orchestrator: FS pass → byte accounting → carve pass | L |
| story-0019 → | see [story-0019](stories/story-0019-candidate-index-arbitration.md): File-backed candidate index + confidence arbitration (ADR-0006) | L |
| story-0016 → | see [story-0016](stories/story-0016-recovery-sink.md): `RecoverySink`: naming, destination validation, extraction (winners only; content de-duplication moved to story-0062, where the SHA-256 it needs is computed) | M |
| story-0017 → | see [story-0017](stories/story-0017-undelete-cli.md): Minimal `revenant-undelete` CLI (modes, source, destination) | M |
| story-0018 → | see [story-0018](stories/story-0018-carve-cli.md): Minimal `revenant-carve` CLI (format allowlist, destination) | S |
| story-0060 → | see [story-0060](stories/story-0060-output-safety.md): Output safety — `sanitizeOutputPath` (confinement) + bounded allocation (ADR-0009) | M |
| story-0061 → | see [story-0061](stories/story-0061-filename-decoding.md): NTFS filename decoding (UTF-16) + safe/disambiguated output names (ADR-0010) | M |
| story-0062 → | see [story-0062](stories/story-0062-session-manifest.md): Session manifest: provenance + SHA-256 + bad-sector map (recovery-output) | M |
| story-0063 → | see [story-0063](stories/story-0063-dry-run.md): `--dry-run` / preview (winners without extraction) | S |
| story-0064 → | see [story-0064](stories/story-0064-resumable-scan.md): Resumable scan: durable, crash-consistent candidate index + checkpoint (ADR-0008) | L |

## Notes

- Scope is deliberately one filesystem (NTFS) and one format (JPEG). Breadth is M2/M3.
- Every parser (MFT, attributes, runlist, JPEG) ships with unit + fuzz tests per
  [ADR-0003](../architecture/adr/adr-0003-validating-carving.md).
- Candidates are indexed and arbitrated, not eagerly extracted, per
  [ADR-0006](../architecture/adr/adr-0006-candidate-arbitration-deferred-extraction.md);
  this proves the deferred-extraction model on the vertical slice.
