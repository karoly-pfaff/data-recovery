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
| story-0101 | see [story-0101](stories/story-0101-partition-view.md): MBR/GPT-free single-partition mount for the test image | S |
| story-0102 | see [story-0102](stories/story-0102-ntfs-boot-sector.md): NTFS boot sector + `$MFT` locator | M |
| story-0103 → | see [story-0103](stories/story-0103-jpeg-validating-carver.md): JPEG validating carver | M |
| story-0104 | see [story-0104](stories/story-0104-ntfs-mft-record.md): NTFS MFT record + attribute parser (`$STANDARD_INFORMATION`, `$FILE_NAME`) | L |
| story-0105 → | see [story-0105](stories/story-0105-ntfs-runlist-decoder.md): NTFS `$DATA` runlist decoder (resident + non-resident) | M |
| story-0118 → | see [story-0118](stories/story-0118-ntfs-image-builder.md): NTFS synthetic-image builder in tools/imagegen | L |
| story-0106 → | see [story-0106](stories/story-0106-ntfs-entry-enumeration.md): NTFS deleted-entry enumeration + path reconstruction | M |
| story-0107 → | see [story-0107](stories/story-0107-carver-registry-and-scan.md): `CarverRegistry` + streaming signature scan | M |
| story-0108 → | see [story-0108](stories/story-0108-hybrid-orchestrator.md): Hybrid orchestrator: FS pass → byte accounting → carve pass | L |
| story-0112 → | see [story-0112](stories/story-0112-candidate-index-arbitration.md): File-backed candidate index + confidence arbitration (ADR-0006) | L |
| story-0109 → | see [story-0109](stories/story-0109-recovery-sink.md): `RecoverySink`: naming, destination validation, extraction (winners only; content de-duplication moved to story-0115, where the SHA-256 it needs is computed) | M |
| story-0110 → | see [story-0110](stories/story-0110-undelete-cli.md): Minimal `revenant-undelete` CLI (modes, source, destination) | M |
| story-0111 → | see [story-0111](stories/story-0111-carve-cli.md): Minimal `revenant-carve` CLI (format allowlist, destination) | S |
| story-0113 → | see [story-0113](stories/story-0113-output-safety.md): Output safety — `sanitizeOutputPath` (confinement) + bounded allocation (ADR-0009) | M |
| story-0114 → | see [story-0114](stories/story-0114-filename-decoding.md): NTFS filename decoding (UTF-16) + safe/disambiguated output names (ADR-0010) | M |
| story-0115 → | see [story-0115](stories/story-0115-session-manifest.md): Session manifest: provenance + SHA-256 + bad-sector map (recovery-output) | M |
| story-0116 → | see [story-0116](stories/story-0116-dry-run.md): `--dry-run` / preview (winners without extraction) | S |
| story-0117 → | see [story-0117](stories/story-0117-resumable-scan.md): Resumable scan: durable, crash-consistent candidate index + checkpoint (ADR-0008) | L |

## Notes

- Scope is deliberately one filesystem (NTFS) and one format (JPEG). Breadth is M2/M3.
- Every parser (MFT, attributes, runlist, JPEG) ships with unit + fuzz tests per
  [ADR-0003](../architecture/adr/adr-0003-validating-carving.md).
- Candidates are indexed and arbitrated, not eagerly extracted, per
  [ADR-0006](../architecture/adr/adr-0006-candidate-arbitration-deferred-extraction.md);
  this proves the deferred-extraction model on the vertical slice.
