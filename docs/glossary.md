<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Glossary

Shared vocabulary for the project. When a term here appears in code or docs, it means
exactly this.

- **Carving** — recovering files from raw bytes by recognizing their content, without
  using filesystem metadata. Contrast with *undelete*.
- **Validating carving** — Revenant's approach: parsing a candidate's internal structure
  to compute its exact extent and confirm validity, instead of collecting bytes until a
  footer or size cap. See [ADR-0003](architecture/adr/adr-0003-validating-carving.md).
- **Extent** — a contiguous run of bytes on the device `(offset, length)` belonging to a
  file.
- **Signature / magic bytes** — a byte pattern that marks a likely file header (e.g.
  `FF D8 FF` for JPEG). A *hypothesis* that triggers validation, not proof of a file.
- **False positive** — bytes emitted as a recovered file that are not actually a valid,
  intended file (e.g. an oversized "SWF"). The primary failure mode Revenant targets.
- **Undelete** — recovering deleted files using surviving filesystem metadata, preserving
  original names, paths, and timestamps.
- **Hybrid mode** — filesystem undelete first, then carving over unallocated space; the
  default of `revenant-undelete`. See
  [hybrid-orchestration](architecture/hybrid-orchestration.md).
- **`BlockDevice`** — the interface for any read-only, random-access byte source
  (image file, physical disk, logical volume).
- **`FileSystem`** — the interface for a read-only filesystem parser that enumerates live
  and deleted entries.
- **`FormatCarver`** — the interface for one file format's recognizer/validator in the
  carve engine.
- **`RecoverySink`** — the component that writes recovered artifacts to the destination,
  handling naming, dedup, and directory reconstruction.
- **Candidate** — a validated match (region, format, confidence) recorded in the candidate
  index, awaiting arbitration; not yet an extracted file. See
  [ADR-0006](architecture/adr/adr-0006-candidate-arbitration-deferred-extraction.md).
- **Arbitration** — resolving overlapping candidates by confidence so only winners are
  extracted; weak secondary matches lose to primaries.
- **Session manifest** — the machine-readable (JSON) record of a run: per-artifact
  provenance, source extents, original vs. written name, confidence, SHA-256, timestamps,
  plus the bad-sector map and scan coverage. See [recovery-output](architecture/recovery-output.md).
- **Provenance** — how an artifact was recovered (filesystem entry vs. carved, and by
  which parser), recorded in the manifest.
- **Dry-run / preview** — a full scan + arbitration that writes nothing, emitting the
  manifest of what *would* be recovered.
- **Bad-sector map** — the record of device ranges that could not be read, tolerated
  during scanning and reported in the manifest.
- **Checkpoint / resume** — durable session state (candidate index + scan cursor) that
  lets an interrupted run continue instead of restarting. See
  [ADR-0008](architecture/adr/adr-0008-resumability-checkpointing.md).
- **`sanitizeOutputPath`** — the single choke-point that confines every recovered filename
  to the destination directory (anti path-traversal). See
  [ADR-0009](architecture/adr/adr-0009-output-safety.md).
- **`Result<T>`** — the typed error-or-value type used for all fallible operations;
  errors are values, not exceptions.
- **Orphaned entry** — a deleted filesystem entry whose parent directory is also gone;
  recovered under a `lost+found`-style path.
- **Recoverability grading** — the `Valid` / `Uncertain` / `Rejected` verdict attached to
  a recovered entry or carved candidate, expressing confidence.
- **MFT (`$MFT`)** — NTFS Master File Table; the record store from which deleted NTFS
  files are recovered.
- **Runlist** — NTFS encoding of a non-resident attribute's data extents on disk.
- **Quality gate** — an automated CI check that must pass to merge; see
  [quality-gates](testing/quality-gates.md).
- **Story / Epic** — a small deliverable unit of work / a milestone-sized group of
  stories; see [backlog](backlog/README.md).
- **ADR** — Architecture Decision Record; see
  [ADR-0001](architecture/adr/adr-0001-record-architecture-decisions.md).
