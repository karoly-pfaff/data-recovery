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

The bad-sector map records **ranges** — `{"offset": N, "length": N}`, device-absolute —
taken verbatim from the run's `SourceStack`. story-0115 chose bare offsets and named the
condition for changing that: bounding the damage needs a reader that survives the fault,
and stating a length before one existed would have made the manifest confidently wrong.
story-0604 put such a reader into every run, so the condition is met and the map says how
far the damage runs.

Each artifact carries the same shape under **`invented`**: the parts of its own extents
that fall inside that map, empty for an artifact that is entirely the device's own bytes.
A file whose middle the drive refused is still written — recovery proceeds past damage —
with zeros where the bytes should be, and `invented` is what stops that from reading as
clean. It is a fact about bytes rather than a fourth `Confidence`: validation answers
whether the structure holds, and a JPEG whose entropy-coded middle is invented zeros can
pass it. `sha256` keeps its meaning, verifying the file that was written; `invented` says
how far to trust what was hashed. The run summary prints a `damage:` line whenever the map
is non-empty, so a run that zero-filled anything cannot be mistaken for one that did not.

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
- **Destination ≠ source, on different storage.** Enforced in two tiers. The path tier
  runs for every source: the output tree must not grow around the source it reads. The
  identity tier runs for a *device* source, which the path tier cannot answer for —
  `\\.\PhysicalDrive0` shares no path element with `C:\recovered` — and compares the
  destination's disk extents against the storage the source reads. A destination on a
  *sibling* volume of the same disk is allowed on purpose: the loss mode is overwriting
  the clusters under recovery, and a sibling volume holds none of them. On Linux the
  destination is traced through the mount table to the device its filesystem was mounted
  from, and a mapped or RAID device through the kernel's `slaves` links to the disks
  underneath it — because such a device reports itself as a disk of its own and would
  otherwise look unrelated to the disk it sits on, and because a filesystem's own device
  number is not its storage's. An identity that cannot be resolved refuses the run rather
  than being assumed safe; only a filesystem type known to hold no local storage — a
  network share, a tmpfs — is allowed on the strength of holding none. The containers this
  does not see through are listed once, in
  [ADR-0012](adr/adr-0012-destination-rule-two-tiers.md), which is the authority for this
  rule; they are not restated here, because a list kept in two places is a list that
  disagrees with itself.
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
