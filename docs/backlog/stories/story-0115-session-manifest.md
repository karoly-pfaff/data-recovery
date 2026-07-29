<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0115: Session manifest — provenance, SHA-256, bad sectors

- Epic: [epic-m1-vertical-slice](../epic-m1-vertical-slice.md)
- Status: Done
- Size: M

## Goal

Make a run **auditable**. Today a recovery leaves a directory of files and three
lines on stderr; what a forensic user needs is a durable, machine-readable record
of *what was found, from where, and whether the bytes are the bytes* — so a
recovery can be verified after the fact by someone who did not watch it happen.

This is also where the content-hash de-duplication deferred by
[story-0109](story-0109-recovery-sink.md) lands, because it needs the same
SHA-256 the manifest does and building two hashes would be the duplication the
contract forbids.

## Design references

- [Recovery output](../../architecture/recovery-output.md) — the manifest's
  contents, field by field: provenance, source extents, original and written
  name, confidence, integrity hash, timestamps, plus the run's bad-sector map.
- [Hybrid orchestration](../../architecture/hybrid-orchestration.md) — "a carved
  file that is byte-identical to a named recovery is dropped in favour of the
  named one. Names are strictly better than `f0000001.jpg`."
- [ADR-0008](../../architecture/adr/adr-0008-resumability-checkpointing.md) — the
  session directory holds the run's durable state; the manifest joins the index
  there.
- [ADR-0009](../../architecture/adr/adr-0009-output-safety.md) — nothing the
  manifest writes is sized by device data.

## Scope

1. **SHA-256** — `revenant::Sha256` (FIPS 180-4), streaming, plus `sha256()` and
   `toHex()`. The core had CRC-32 for chunk integrity; an artifact hash is a
   different job and needs a real cryptographic digest.
2. **Hashing where the bytes already flow** — `extractTo` digests each artifact
   as it writes it, so a manifest costs no extra pass over the recovered data.
3. **De-duplication** — a carved artifact whose content is byte-identical to one
   already written is dropped in favour of it, and counted.
4. **The record** — `ArtifactRecord`: provenance, names, extents, size, hash,
   confidence, timestamps, and what became of it.
5. **The manifest** — `writeManifest`, emitting `manifest.json` into the session
   directory next to the candidate index.
6. **Both frontends** write one, and report the de-duplication count.

## Design decisions

**Named artifacts are written first, but numbered in device order.** De-duplication
has to be able to say "the named one wins", and the cheapest way to guarantee that
in a single pass is to write the named artifacts before the carved ones. Ordinals
still come from the winner's position in the device-ordered list, so the names on
disk are exactly what they were before: two runs over one device still produce the
same output. Write *order* changed; output did not.

**A duplicate is written and then removed, not read twice.** Nothing can know an
artifact is a duplicate until its last byte has been hashed, and a recovery tool
may not buffer a whole file to find out (ADR-0009). The alternatives are reading
every carved artifact twice — which is the entire cost of a carve-only run — or
writing it and removing it once the digest turns out to be a repeat. The second is
one pass, and the file is gone before the run reports anything.

**Only carved artifacts are ever de-duplicated.** Two named files with identical
content are two real files with two real names, and dropping either would be data
loss dressed up as tidiness. What the architecture calls de-duplication is
specifically the anonymous copy of something already recovered *with* a name.

**The bad-sector map records offsets, not ranges.** A range needs a reader that
survives a fault and probes forward for where the damage ends — that is
`RetryingDevice` and imaging mode (story-0402, story-0048, M4). What this build
can honestly say today is the device offset at which a read stopped, so that is
what the manifest states. Inventing a length would make the manifest confidently
wrong, which is worse than incomplete.

**Suppressed candidates are a count, not records.** `arbitrate` reports how many
candidates a better explanation displaced, not which ones — a deliberate
story-0112 decision, since holding every loser is the unbounded allocation
ADR-0009 forbids. The manifest states the count; per-loser detail belongs to
whatever story makes arbitration stream its decisions.

**The manifest is written by hand, not by a JSON library.** The document has six
value types and no parsing side: adding a dependency and its supply-chain
review (story-0008) to emit it would cost more than the forty lines of escaping
it replaces.

## Acceptance criteria

### `Sha256`

- [x] Matches the published FIPS 180-4 vectors for the empty input, `"abc"`, and
      the 56-byte two-block message.
- [x] Streaming and one-shot hashing of the same bytes agree, whatever the chunk
      boundaries.
- [x] Handles an input that lands exactly on a block boundary, and one whose
      length forces a second padding block.
- [x] `toHex` renders 64 lowercase hex characters.
- [x] Fuzz target: incremental hashing equals one-shot for any input and split.

### Extraction

- [x] `extractTo` returns the bytes written *and* the SHA-256 of what it wrote.
- [x] The digest of a written file equals the digest of its bytes on disk.
- [x] A carved winner duplicating an already-written artifact leaves no file
      behind and is counted in `ExtractionStats::deduplicated`.
- [x] A named winner is never de-duplicated, even against an identical one.
- [x] Named winners are written before carved ones; carved ordinals are
      unchanged.

### The manifest

- [x] `writeManifest` emits `manifest.json` into the session directory.
- [x] Each artifact records its provenance (`filesystem` / `carve`), original
      name, written name, extents, size, hash, confidence, timestamps, and
      outcome (`written` / `deduplicated` / `failed`).
- [x] A failed artifact records no hash and no written name, rather than a
      misleading one.
- [x] The run records the source, the destination, the mode, the winner and
      suppressed counts, and the offsets at which a read failed.
- [x] Names containing quotes, backslashes, or control characters are escaped,
      so a hostile filename cannot break the document.

## Test plan

Unit (`tests/unit/core/Sha256Test.cpp`): the three published vectors; one-shot
against streaming with several chunkings; a 64-byte input (exact block); a
55-byte and a 56-byte input (the padding boundary); `toHex` shape.

Fuzz (`tests/fuzz/Sha256Fuzz.cpp`): hashing the input in one call and in two
calls split at a fuzzer-chosen point must agree.

Unit (`tests/unit/recovery/ManifestTest.cpp`): a manifest round-tripped as text —
every field present; a failed artifact with no hash; a name with a quote, a
backslash and a newline in it; an empty run.

Unit (`tests/unit/recovery/RecoverySinkTest.cpp`): a carved winner duplicating a
named one is dropped and counted, and no file is left behind; two identical named
winners both survive; carved ordinals are unaffected by the write order.

Integration (`tests/integration/UndeleteCliTest.cpp`,
`tests/integration/CarveCliTest.cpp`): a real run leaves a `manifest.json` in the
session directory naming the files it recovered, with the hash of the deleted
JPEG matching the fixture's own bytes.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] Coverage held or raised (>= 85% core) — enforced by the CI coverage job;
      the instrumentation is GCC/Clang-only, so it is not reproducible on the
      local MSVC build.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` + `docs/architecture/recovery-output.md` updated.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Epic row linked.
