<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0108: Hybrid orchestrator — FS pass → byte accounting → carve pass

- Epic: [epic-m1-vertical-slice](../epic-m1-vertical-slice.md)
- Status: Done
- Size: L

## Goal

Make the two recovery sources one run. Filesystem recovery is precise but
fragile; carving is robust but anonymous. Hybrid mode gets both: recover what
the metadata can name ([story-0106](story-0106-ntfs-entry-enumeration.md)), then
carve the space those names did not account for
([story-0107](story-0107-carver-registry-and-scan.md)) — which is the claim
"better than the sum of PhotoRec and TestDisk" actually rests on.

## Design references

- [Hybrid orchestration](../../architecture/hybrid-orchestration.md) — the
  strategy, the three modes, and the rule that byte accounting is a
  *performance* optimization while arbitration is the *correctness* authority.
- [ADR-0006](../../architecture/adr/adr-0006-candidate-arbitration-deferred-extraction.md) —
  discovery, not extraction: the orchestrator reports, it never writes.
- [ADR-0009](../../architecture/adr/adr-0009-output-safety.md) — the accounted
  set is sized by on-disk data and therefore bounded.

## Scope

Three units, one seam each.

1. **Bounded signature search** — `ScanRegion` +
   `SignatureScanner::scanRegion`. A region says where to *look* for a
   signature, not what a file may be: a candidate that starts inside the region
   is still carved to its true length, even when that runs past the region's
   end. Truncating a file at a region boundary would turn a `Valid` recovery
   into an `Uncertain` fragment for no reason — the boundary is an artifact of
   what some other file claimed, not of this file.
2. **Byte accounting** — `recovery::ByteAccounting`. Collects the extents of
   confidently recovered entries into a merged region set and returns its
   complement: the only places worth searching. An `Uncertain` entry does **not**
   suppress carving; the architecture calls that region a safety net, and it is.
3. **Sequencing** — `recovery::HybridRecovery`. Owns the order of the two
   passes and nothing else: every parse stays in its own layer.

**Deliberately out of scope:**

- The candidate index and arbitration ([story-0112](../epic-m1-vertical-slice.md)).
  The orchestrator reports to the visitors that already exist; swapping those
  for the index is that story's job, and inventing its shape here would be
  guesswork.
- Extraction, naming, and dedup (story-0109) — nothing is written.
- Partition discovery. One volume per run; MBR/GPT is M4.

## Design decisions

**A hybrid run over a volume with no readable filesystem still carves.** A
formatted or RAW volume is precisely the case carving exists for, so an
unmountable filesystem downgrades the run rather than failing it — but it is
reported (`RecoveryStats::filesystemMounted`), never silent. In `--fs-only`
the same failure *is* the result, and propagates as the typed error it is.

**Accounting is capped, and says when it gave up.** The accounted set is sized
by on-disk data (records × runs), so ADR-0009 requires a bound. Past
`kMaxAccountedRegions` further extents are dropped and counted
(`RecoveryStats::regionsDropped`). Dropping is safe in a way that dropping a
*candidate* would not be: less accounting means more scanning, never less.

## Acceptance criteria

### `ScanRegion` / `scanRegion`

- [x] `ScanRegion{offset, lengthBytes}` names where a scan looks for signatures.
- [x] `SignatureScanner::scanRegion` reports only candidates that **start**
      inside the region, and reads no window past what the region plus one
      signature's width needs.
- [x] A candidate starting inside the region keeps its full carved length even
      when the file runs past the region's end.
- [x] A signature straddling the region's end boundary is still found when its
      candidate start falls inside the region.
- [x] `scan(device, visitor)` is `scanRegion` over the whole device, so
      story-0107's behaviour is unchanged (its tests are untouched).
- [x] An empty region reports nothing and reads nothing.

### `ByteAccounting`

- [x] `account(entry)` takes the extents of an entry graded `kValid`; an entry
      graded `kUncertain` contributes nothing.
- [x] Overlapping and touching regions merge, so the set stays proportional to
      distinct regions rather than to file count.
- [x] `gaps(deviceSize)` returns the complement within `[0, deviceSize)`, in
      offset order, with no empty regions.
- [x] A region reaching past `deviceSize` is clipped by `gaps`; a fully
      out-of-range one contributes no gap boundary.
- [x] Nothing accounted yields one gap covering the whole device.
- [x] The region count is capped at `kMaxAccountedRegions`; extents past it are
      dropped and counted, not accepted unchecked (ADR-0009).

### `HybridRecovery`

- [x] `RecoveryMode` is `kFilesystemOnly`, `kHybrid`, or `kCarveOnly`.
- [x] `kFilesystemOnly` runs the filesystem pass and no scan; a volume that
      will not mount is a typed error.
- [x] `kCarveOnly` runs the scan over the whole device and never touches the
      filesystem.
- [x] `kHybrid` runs the filesystem pass, accounts the confident entries, and
      scans only the gaps.
- [x] A hybrid run over an unmountable volume still scans the whole device, and
      reports `filesystemMounted == false`.
- [x] `RecoveryStats` reports entries, candidates, accounted bytes, regions
      scanned, regions dropped, and whether the filesystem mounted.
- [x] Nothing is extracted or written; both passes report to visitors.

## Test plan

Unit (`tests/unit/carve/ScanRegionTest.cpp`): a candidate before the region is
not reported; one inside is; one starting past the end is not; a file starting
near the end keeps its full length; an empty region reports nothing; a magic
whose candidate start is the region's last byte is still found.

Unit (`tests/unit/recovery/ByteAccountingTest.cpp`): nothing accounted → one
whole-device gap; one region in the middle → two gaps; adjacent and overlapping
regions merge; a region at offset 0 and one at the device end leave no leading
or trailing gap; an `kUncertain` entry is not accounted; a region past the
device end is clipped; the cap drops and counts.

Unit (`tests/unit/recovery/HybridRecoveryTest.cpp`): the three modes over the
fixture volume, each asserted on what it reports; an unmountable volume in
hybrid mode still carves and reports `filesystemMounted == false`; the same
volume in `kFilesystemOnly` is a typed error.

Integration (`tests/integration/HybridRecoveryTest.cpp`): the story-0118 fixture
image through `ImageFileDevice`. Hybrid recovers the four named files **and**
carves the JPEG in unallocated space that no record points at — the one file
neither source finds alone. Carve-only finds every JPEG on the volume including
the named ones; fs-only finds no candidates at all. The carve pass reports no
candidate inside a region a `kValid` entry already accounts for.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan.
- [x] Coverage held or raised (>= 85% core) — enforced by the CI coverage job;
      the instrumentation is GCC/Clang-only, so it is not reproducible on the
      local MSVC build.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` + `docs/architecture/hybrid-orchestration.md` updated.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Epic row linked.
