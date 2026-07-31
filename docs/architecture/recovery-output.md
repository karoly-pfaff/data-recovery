<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Recovery Output — Manifest, Modes & Scaling

This document covers what happens on the way out: how recovered artifacts are reported,
how a run can be previewed without writing, and how the destination copes with large
recoveries. It complements [hybrid orchestration](hybrid-orchestration.md) (which decides
*what* is recovered) and [output safety](adr/adr-0009-output-safety.md) (which keeps
writing safe).

## The session manifest

Every recovery run produces a durable, **machine-readable manifest** (JSON) in the
session directory — the authoritative, forensically-useful record of the run. Per
artifact it records:

- **Provenance:** how it was recovered — a filesystem entry (which filesystem, MFT record
  / directory entry) or a carved candidate (which `FormatCarver`).
- **Source location:** the device extent(s) `(offset, length)` the data came from.
- **Original name** (decoded, [ADR-0010](adr/adr-0010-filename-decoding-safe-output.md))
  and the **written name** (sanitized), so a rename is visible, not silent.
- **Confidence** verdict and the arbitration outcome (winner, or why suppressed).
- **Integrity hash:** a SHA-256 of the recovered bytes, so output can be verified and
  de-duplicated after the fact.
- **Timestamps** (where the filesystem preserved them).

The run also records device-level facts: the **bad-sector map** and scan coverage.
Together these make a recovery **auditable and reproducible** — you can see exactly what
was found, from where, and how much of the device was actually read.

The bad-sector map currently records the device **offsets** a read stopped at, not the
ranges around them. Bounding the damage needs a reader that survives a fault and probes
forward for where it ends — `RetryingDevice` and imaging mode, both M4 — and stating a
length before then would make the manifest confidently wrong rather than merely
incomplete.

Suppressed candidates appear as a count rather than as records: `arbitrate` reports how
many a better explanation displaced, not which ones, because holding every loser is the
unbounded allocation [ADR-0009](adr/adr-0009-output-safety.md) forbids.

## Modes: dry-run / preview

`--dry-run` performs the full scan, validation, and arbitration but **writes no
artifacts**. It emits the manifest of what *would* be recovered — the winner set, with
names, sizes, confidences, and provenance. Because extraction is deferred until after
arbitration ([ADR-0006](adr/adr-0006-candidate-arbitration-deferred-extraction.md)), a
preview is simply "stop before extraction", so it is cheap to offer and accurate.

This lets an operator inspect and refine (adjust the format allowlist or confidence
threshold) before committing disk space and time to extraction — and, with
[resumability](adr/adr-0008-resumability-checkpointing.md), a preview can later be
promoted to a real extraction without re-scanning.

## Destination & scaling

Real recoveries can produce **millions of small files**, which breaks naïve output:

- **Free-space check up front.** Before extraction the CLI verifies the destination has
  room (estimated from the winner set) and refuses to start a doomed run.
- **Directory sharding.** Output is bucketed so no single directory holds an unwieldy
  number of entries (e.g. by type and a counter range), staying within filesystem limits
  and keeping the tree browsable.
- **Destination ≠ source, on different storage.** Enforced, in two tiers, because a
  source is one of two things. A *device* source is compared by physical identity: the
  destination's disk extents against the storage the source reads, so
  `\\.\PhysicalDrive0` and `C:\recovered` are recognised as the same disk even though
  they share no path element. A destination on a *sibling* volume of the same disk is
  allowed on purpose — the loss mode is overwriting the clusters under recovery, and a
  sibling volume holds none of them. An *image-file* source keeps the path rule: the
  output tree must not grow around the image it reads, and a destination sharing a volume
  with an image is normal practice. When a device source's identity cannot be resolved,
  the run is refused rather than assumed safe.
- **Layout policy.** Named entries reconstruct their directory tree (confined to the
  destination); carved entries go into type buckets — `carved/<ext>/f<ordinal>.<ext>`,
  numbered in device order so two runs over one device produce the same names. A carver's
  extension is data, so it names a bucket only if it looks like one of ours; anything
  else lands in `carved/bin/` rather than steering the path. Policy lives in `recovery/`.
- **Collisions are renamed, not overwritten.** Two winners wanting one path are resolved
  by the ADR-0010 suffixing rule, and the rename is counted so it is visible in the run's
  stats rather than silent.
- **Named artifacts are written first, but numbered where they stand.** De-duplication
  has to be able to say "the named one wins", and writing the named artifacts first is
  what guarantees a carved duplicate arrives second in a single pass. Ordinals still come
  from the winner's place in device order, so two runs over one device produce the same
  names regardless.
- **A short write is a failure, not a smaller file.** An extraction either lands whole or
  is counted as failed; a recovery tool that quietly writes truncated files is worse than
  one that stops.

## Where this lives

- Manifest, hashing, provenance, sharding, and free-space checks are in the `recovery/`
  layer (the `RecoverySink` and reporting components).
- The CLI exposes `--dry-run`, the destination, the format allowlist, and the confidence
  threshold as flags that map onto this policy — it holds no policy of its own.
