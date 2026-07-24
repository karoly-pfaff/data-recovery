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

The run also records device-level facts: the **bad-sector map** (which ranges were
unreadable) and scan coverage. Together these make a recovery **auditable and
reproducible** — you can see exactly what was found, from where, and how much of the
device was actually read.

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
- **Destination ≠ source, on different storage.** Enforced; recovered data must not be
  written onto the media being recovered.
- **Layout policy.** Named entries reconstruct their directory tree (confined to the
  destination); carved entries go into type buckets. Policy lives in `recovery/`.

## Where this lives

- Manifest, hashing, provenance, sharding, and free-space checks are in the `recovery/`
  layer (the `RecoverySink` and reporting components).
- The CLI exposes `--dry-run`, the destination, the format allowlist, and the confidence
  threshold as flags that map onto this policy — it holds no policy of its own.
