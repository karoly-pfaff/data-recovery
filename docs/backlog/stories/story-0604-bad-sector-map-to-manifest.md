<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0604: A hole is not a zero: the bad-sector map reaches the manifest and the candidates

- Epic: [epic-m6-loose-ends](../epic-m6-loose-ends.md)
- Status: In review
- Size: L

## Goal

Compose the I/O decorators into real runs — for the first time — and make the
bad-sector map they produce reach the session manifest, so that a recovered file
whose middle the device refused to give up is recorded as *degraded* rather than
written out as clean. Zero-filling an unreadable sector stays, because recovery
must proceed past damage; what goes is the silence about having done it.

## Design references

- [ADR-0003](../../architecture/adr/adr-0003-validating-carving.md) — precision
  over recall is the project's founding claim. A tool that invents bytes and does
  not say so breaks it, whatever its parsers validate.
- [ADR-0007](../../architecture/adr/adr-0007-block-level-access-boundary.md) —
  `BlockDevice` is the capability boundary; this story decides how the bad-sector
  map crosses it without widening it.
- [story-0402](story-0402-io-decorators.md) — built `RetryingDevice` and
  `CachingDevice`, and deferred their composition: "wiring it is the story that
  puts these decorators into a run" (lines 78–79). This is that story.
- [story-0115](story-0115-session-manifest.md) — the manifest, and its explicit
  "offsets, not ranges" decision for the bad-sector map, made because no reader
  that survives a fault existed to state a length honestly. That reader has
  existed since M4; it was never plugged in.
- [story-0109](story-0109-recovery-sink.md) /
  [story-0112](story-0112-candidate-index-arbitration.md) — the candidate and
  artifact records this story extends.
- [I/O layer](../../architecture/io-layer.md) and
  [recovery output](../../architecture/recovery-output.md) — both describe the
  decorators in the present tense; both are ahead of the code and updated here.

## What was measured

The epic's paragraph for this story calls it "a defect in shipped code": a
`RetryingDevice` in the stack zero-fills a bad sector, returns success, and
thereby empties the manifest's `unreadable` list. The M5 architecture audit
checked, twice, and the premise does not hold — **nothing puts a
`RetryingDevice` in the stack**. Verified against the current tree:

- `openSource` (`src/core/io/SourceDevice.cpp:49-57`) builds a bare
  `ImageFileDevice` or `RawDevice` and nothing else. Both production call sites —
  `src/cli/RecoveryRun.cpp:180-188` and `src/cli/PartitionListing.cpp:72` — read
  through the bare device. The only constructor calls of either decorator in the
  tree are in `tests/unit/io/RetryingDeviceTest.cpp` and
  `tests/unit/io/CachingDeviceTest.cpp`.
- The decorators take non-owning references
  (`include/revenant/core/io/RetryingDevice.hpp:45`, member at `:65`;
  `CachingDevice.hpp:42`, member at `:84`) while `openSource` returns
  `Result<std::unique_ptr<BlockDevice>>`. No owning composed stack exists
  anywhere; there is nothing a caller could even hold.
- `badRanges()` (`RetryingDevice.hpp:55`, `src/core/io/RetryingDevice.cpp:27`)
  has no consumer outside the decorator's own tests. A tree-wide grep finds its
  name in tests, docs, and the epic's accusation.
- Consequently, in shipped binaries a failed read **does** propagate and **does**
  populate the manifest: `src/recovery/RecoverySink.cpp:121-126` records the
  failure and pushes the offset into `unreadable`, which flows through
  `src/cli/RunDelivery.cpp:79` into `src/recovery/Manifest.cpp:132`. The shipped
  tool is honest by accident — it never fabricates because it never survives.
- The map it can state is threadbare: `SessionManifest::unreadable` is
  `std::vector<std::uint64_t>` — bare offsets
  (`include/revenant/recovery/Manifest.hpp:32-34`), serialized by `offsetsJson`
  (`Manifest.cpp:116-123`). story-0115 chose that deliberately and named the
  upgrade condition; `RecoverySink.hpp:47-51` still carries the same comment.
- `CHANGELOG.md:164-168`, inside the released `[0.3.0]` section, credits
  `RetryingDevice` with surviving "a drive that will not answer" — behavior no
  shipped binary has. `docs/architecture/io-layer.md:117-125` and
  `docs/architecture/recovery-output.md:31-35` make the same promise in prose.
- `src/carve/WindowMatch.cpp` stands at 208 of the 250-line hard limit, holding
  the portable walk, the AVX2 batch machinery, and window IO
  (`readWindow`/`readAndMatch` at `:183-206`) — the file an unwary
  implementation would grow candidate marking into.
- The fault-injecting device from story-0402 is `tests/support/FaultyDevice.hpp`:
  per-range faults, transient or permanent, with a read counter.

## Design decisions

**The epic's diagnosis is corrected, not inherited.** The defect is latent, not
shipped: the fabrication scenario becomes real the moment anyone composes a
`RetryingDevice` without also connecting `badRanges()` — and story-0402's own
text scheduled exactly that composition for a later story. The story keeps its
place in M6 and its size L for the reason the epic gave, minus the false alarm:
the decorators cannot be wired in *safely* until the map has somewhere to go,
and three released documents already claim they are wired in. This story makes
the claim true and the wiring safe in the same change.

**An owning stack type, not a wider `BlockDevice`.** A `SourceStack` in
`core/io/` owns the concrete device and the decorators over it — members held
by `unique_ptr` so the non-owning references inside the decorators survive a
move — and exposes `BlockDevice& top()` plus
`std::span<const BadRange> badRanges()`. The alternative, a `badRanges()` with a
default-empty answer on `BlockDevice` itself, fails ADR-0007's boundary twice:
it makes every implementation answer a question only one wrapper can, and the
answer would depend on which layer of a composition you happen to hold — a
`PartitionView` over a `RetryingDevice` would report *no damage* while sitting
on top of it. That is the silent-lie shape this story exists to remove. The
interface stays the tiny read seam the I/O layer promises; the map belongs to
the one object that can answer for the whole composition, which is the thing
that composed it.

**The stack is always composed for real runs.** `openSource` remains the single
place a source path becomes a device and now returns `Result<SourceStack>`; both
CLI call sites take the stack. No flag, no second path: an image on a network
share wants the retry-and-cache treatment as much as a raw disk does — ADR-0007
says so in as many words — and a production path without the decorators is the
path this story exists to retire. The mismatch between `unique_ptr` ownership
and reference-taking decorators dissolves inside the stack instead of leaking
into every caller.

**Retry sits nearest the device; the cache sits on top.** A bad sector is then
cheap: the retry layer's sector-by-sector narrowing runs against the real device
rather than having each attempt amplified into a whole-block re-read through a
cache, and the zero-filled block the cache keeps spares the drive every repeat
read while that block is resident. story-0402 proved the decorators stack in
either order; production picks this one. `RetryPolicy` and `CacheShape` keep
their defaults — an operator flag for retry attempts is a story for whoever
needs it.

**The map is a set, and making it one was a defect this story flushed out.** The
paragraph above first said a bad sector is "paid for once", full stop. It is
not: the cache holds 4 MiB, so on any real source that block is evicted long
before extraction reads it again — and `RetryingDevice::recordBad` merged a new
range only with the *last* one it had recorded. Composing the stack is what
turned that from a latent wart into a wrong number: the same sector would be
appended twice, doubling the byte total the manifest and the summary report,
listing every overlap twice against the artifact spanning it, and growing
without bound on a failing drive — the shape ADR-0009 forbids. No test could
see it, because the NTFS fixture is *exactly* 4 MiB and so never evicts a
block. `recordBad` now inserts in offset order and coalesces, `badRanges()`
documents itself as a set, and the case is pinned three ways: two reads of one
sector, two reads met out of order, and a stack over a device one block larger
than its cache.

**Degraded is a fact about bytes, not a fourth `Confidence`.** Validation
answers "does the structure hold"; degradation answers "were these the device's
bytes". A JPEG whose entropy-coded middle is invented zeros can pass validation
— that is precisely the case worth flagging. So `ArtifactRecord` grows
`std::vector<BadRange> invented`: the intersections of the artifact's extents
with the run's bad-sector map, empty for the untouched. The manifest writes it
per artifact as `"invented"`; `ExtractionStats` grows a `degraded` count; the
run-level `unreadable` member becomes `std::vector<BadRange>`, the stack's map
verbatim, serialized as `{"offset": N, "length": N}` objects. story-0115's
offsets-only decision is honored by retiring it on its own stated condition: the
reader that can bound the damage honestly now exists and is in the stack. The
`sha256` field keeps its meaning — it verifies the written file, and `invented`
says how far to trust what was hashed.

**Marking happens at delivery, in one coordinate system.** A pure function in
`recovery/` intersects artifact extents with the map, applied where the finished
extraction and the composed stack already meet (`src/cli/RunDelivery.cpp:70-93`).
Bad ranges are device-absolute; extents recorded under a partition run are
view-relative, and the owner of the translation is the scope story-0610 put in
`recovery/`: `RunScope::startBytes()`, which that story deliberately left
unwritten because it had no caller until this one. (The anchor this paragraph
first named, `src/cli/RecoveryRun.cpp:163-165`, is the lambda story-0610
deleted — which is exactly why the ordering said 0610 first.)
The manifest states absolute device offsets — the numbers an operator can check
against any other tool. Resident content never touches the device through its
extents and is never marked. Marking applies to previewed records too: the
intersection is a fact about extents, not about extraction.

**`WindowMatch.cpp` is scoped around, not grown.** Marking at delivery means the
carve scan does not change: zeros carry no signatures, and a candidate spanning
a bad range is caught by extent intersection after arbitration, not by teaching
the matcher about damage. If implementation drifts toward per-window awareness
anyway, the window IO at `:183-206` moves to its own file *first*, as a
preparatory commit — 42 lines of headroom is not where an L story keeps its
margin.

**The CHANGELOG is corrected, not rewritten.** The `[0.3.0]` entry stands —
released sections are history — and `[Unreleased]` gains a correction stating
that the 0.3.0 note described a decorator no shipped binary composed, alongside
this story's entry making it true.

## Acceptance criteria

- [ ] `openSource` returns an owning, composed stack — source device, then
      `RetryingDevice`, then `CachingDevice` — and both production call sites
      consume it; no bare-device production path remains.
- [ ] The stack exposes the bad-sector map for the whole composition, and a run
      over an undamaged source exposes an empty one.
- [ ] The manifest's `unreadable` member carries `{offset, length}` ranges,
      device-absolute, verbatim from the stack — including in a partition run.
- [ ] Every artifact whose extents overlap a bad range carries the overlap in
      its `invented` member; artifacts with no overlap carry none; resident
      content is never marked.
- [ ] The run summary reports the unreadable byte total and the degraded
      artifact count — a run that zero-filled anything cannot end looking like
      one that did not.
- [ ] Zero-fill is preserved: a recovered file spanning a bad sector is written
      whole, with zeros where the device refused, and the run proceeds.
- [ ] `CHANGELOG.md` gains the correction under `[Unreleased]`;
      [io-layer.md](../../architecture/io-layer.md) and
      [recovery-output.md](../../architecture/recovery-output.md) describe the
      composed stack and the range-based map as they are, not as planned; the
      offsets-only comments at `Manifest.hpp:32-34` and `RecoverySink.hpp:47-51`
      go with them.
- [ ] `src/carve/WindowMatch.cpp` is no closer to the 250-line limit than it
      started, or has been split first.

## Test plan

Unit (`tests/unit/io/SourceStackTest.cpp`): a composed stack over a
`FaultyDevice` reads what the bare device would, zero-fills the faulted sectors,
and reports them in the stack's map; a clean source reports an empty map; the
stack remains valid after being moved.

Unit (`tests/unit/recovery/`): the intersection function — an extent spanning a
bad range yields exactly the overlap; a range abutting an extent's first or last
byte marks one byte, one byte further marks nothing; a fault falling in the gap
of a fragmented file's extents marks nothing; a resident-content record is never
marked; view-relative extents intersect correctly after translation. Manifest
JSON: `unreadable` as range objects; `invented` present and correct on a
degraded artifact, empty otherwise; an empty run.

Integration (in-process, over the composed stack on a `FaultyDevice` holding a
carve fixture): a known-bad sector lands inside a JPEG's entropy-coded data —
the file validates, is written with zeros at the fault, its artifact comes out
`written` *and* degraded with the exact overlap, and the manifest carries the
range. Edge placement variants: the fault at a sector boundary, and at a carve
window boundary (`kPrefilterChunkBytes`), where the scan's overlap handling and
the map must agree.

**The carve-window-boundary placement was dropped, and this says why rather than
leaving it silently unbuilt.** What it was for is the case where the scan reads
the same bad sector twice through overlapping windows and the map has to survive
it. That turned out to be a *defect* rather than a placement — the map was a log
of read events rather than a set — and it is now pinned three ways at unit level
(`RetryingDevice.ReadingTheSameBadSectorTwiceRecordsItOnce`,
`RetryingDevice.MergesBadSectorsMetOutOfOrder`, and
`SourceStack.ABadSectorMetAgainAfterEvictionIsStillOneRange`), where a double
encounter is stated directly instead of arranged by arithmetic against a private
chunk size the carve engine is free to change. The sector-boundary variant
stands: the injected fault is one whole aligned sector, which is what that case
is. Four placements were added the plan did not ask for and the audit did: a
scoped run, a preview, an interrupted run, and a device larger than the cache.

Regression: after this story the stack is always composed, so the shipped
"failed reads populate `unreadable`" behavior is superseded by design — a fault
now surfaces as a range instead of a propagated error, and the integration test
above is its replacement. The `kFailed` accounting path
(`RecoverySink.cpp:121-126`) remains for what retry cannot absorb — a refused
destination today, a vanished device when
[story-0605](story-0605-device-loss-mid-run.md) takes it up. Its test did *not*
survive unchanged, contrary to what this paragraph first claimed:
`RecoverySink.RecordsWhereReadingTheSourceFailed` asserted the offset that went
into `Extraction::unreadable`, which no longer exists, so it became
`AWinnerItCannotReadIsCountedAsFailed` and asserts the count and the `kFailed`
outcome instead. Nothing is lost — where the artifact sat is in its own
`extents` — but the claim was wrong and is corrected here rather than left.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan — 1101 Windows,
      1083 Linux.
- [x] clang-format, clang-tidy, duplication and file-length guard clean on both
      platforms. `src/carve/WindowMatch.cpp` is untouched at 208 lines, which is
      the last criterion above.
- [x] `CHANGELOG.md` updated under `[Unreleased]`, including the correction to
      the released 0.3.0 note.
- [x] Epic row linked.
- [x] Story-level self-audit checklist ([code-quality.md](../../code-quality.md))
      completed — two adversarial rounds, and the first one earned its keep. It
      found that the map double-counted a sector met twice and that no fixture
      could see it, that `RunScope::startBytes()` had no test that could fail,
      and that an interrupted run reported no damage. The second found that the
      summary told an operator bytes had been *written* on a preview, and that
      the manifest had come to hold two coordinate systems in one record. All
      six are fixed above, each with a test verified by mutation.
