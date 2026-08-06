<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0701: ADR-0012 records the two-tier destination rule, and ADR-0005 becomes immutable again

- Epic: [epic-m7-hardening](../epic-m7-hardening.md)
- Status: In review
- Size: S

## Goal

An `Accepted` ADR describes the destination check as the exact bug M6 fixed, and the
decision that replaced it was written into a *different* ADR's Consequences in place,
which [ADR-0001](../../architecture/adr/adr-0001-record-architecture-decisions.md)
forbids. Give the real rule an immutable record of its own, and put back the text that
was overwritten.

## Design references

- [`docs/architecture/adr/adr-0011-two-halves-of-the-read-only-guarantee.md`](../../architecture/adr/adr-0011-two-halves-of-the-read-only-guarantee.md)
  — `Accepted`, dated 2026-07-31. Its "Validated" half and its third Consequence are the
  false text.
- [`docs/architecture/adr/adr-0005-read-only-by-default.md`](../../architecture/adr/adr-0005-read-only-by-default.md)
  — the ADR whose Consequences were edited in place at `4a4221e` (+14/−2).
- [`src/recovery/DestinationRule.hpp`](../../../src/recovery/DestinationRule.hpp) /
  [`.cpp`](../../../src/recovery/DestinationRule.cpp) — where the rule actually lives.
  `destinationOnSource` is the whole rule; `refuseOverlap` is the device tier.
- [ADR-0001](../../architecture/adr/adr-0001-record-architecture-decisions.md) and
  [the ADR index](../../architecture/adr/README.md) — both state the immutability rule
  that `4a4221e` broke.
- [story-0609](story-0609-destination-on-source-refused.md) — the work ADR-0011 names as
  future and which has been merged since `4a4221e`.

## What is actually wrong

Three separate defects, all in the record rather than the code:

| # | Where | What it says | What is true |
|:-:|-------|--------------|--------------|
| 1 | ADR-0011, "Validated" half | the rule "is enforced by a lexical path-prefix comparison in `RecoverySink`" | it lives in `recovery/DestinationRule`, and spelling is only the first of two tiers |
| 2 | ADR-0011, same half | "That comparison does not hold for raw-device sources … story-0609 exists to make ADR-0005's sentence true" | story-0609 landed at `4a4221e`, inside M6 |
| 3 | ADR-0005, Consequences | the two-tier rule, written *in place* over the accepted text | an `Accepted` ADR's Decision and Consequences are immutable |

ADR-0011 also instructed its own successor — "When story-0609 lands, the Validated half
becomes a real check and this record should say so" — and was itself edited afterwards
while the stale half three lines above was left untouched. That is the shape of the
failure: an edit that answered the instruction it was given without reading the record it
was editing.

## Design decisions

**A new ADR, not a repair of ADR-0011.** The obvious cheap fix — correct ADR-0011's
paragraph — is the same move that caused the problem, and ADR-0001's rule exists precisely
so that a superseded decision leaves a trace. ADR-0012 is written; ADR-0011's Validated
half is marked superseded by it and its text is left standing as the record of what was
believed on 2026-07-31.

**ADR-0005 is restored, not re-edited.** Its Consequences go back to the two lines
`5079837` accepted:

> Recovered output requires a destination with sufficient free space, on a different
> volume; the CLI validates this before starting.

The fourteen lines that replaced them are not deleted — they are what ADR-0012 says, at
length and in the right place.

Restoring is itself an edit to an `Accepted` ADR, and
[story-0705](story-0705-adr-immutability-check.md)'s check will refuse exactly this shape:
ADR-0012 supersedes ADR-0011's Validated half, **not ADR-0005**, so no new record names
ADR-0005 and neither escape applies. That is the correct verdict for the general case — a
gate loose enough to wave this through would wave through any edit that happened to
accompany a new ADR. **The resolution is sequencing, not an exception: this story lands
first, and story-0705's gate holds from a correct baseline afterwards.** The reasoning and
the two rejected alternatives are recorded in story-0705.

**ADR-0012 states the rule from the code, not from ADR-0005's prose.** The seam is
`DeviceIdentity`/`StorageExtents`; the two tiers are spelling (every source) and physical
identity (device sources only); the failure mode is *refuse when either identity cannot be
resolved*. Each of those is read off `DestinationRule.cpp` and cited by symbol, not by
line — see [story-0706](story-0706-citations-resolve.md) for why.

**It states what it does not catch, in the ADR rather than only in the release notes.**
A Windows Storage Space or mounted VHD answers with the *virtual* disk's extents, so a
destination inside such a container on the source disk is not recognised; on Linux a
loop-mounted image is reported as a disk of its own. Both are already known
([epic-m8](../epic-m8-release.md) notes them for the limits page). An ADR that records
only the cases a rule handles is how the next reader learns the wrong boundary.

**Not in scope: the two other ADR-versus-code claims.** The epic's notes flag ADR-0008
(bad-sector map named as durable session state, not persisted by the checkpoint) and
ADR-0007 (network sources justified by decorators `SourceStack::over` no longer composes).
Both are real and both are the same *kind* of defect, but neither was found by the audit's
confirmed findings and neither is load-bearing for M8's limits page. They stay recorded in
[epic-m6](../epic-m6-loose-ends.md#milestone-architecture-audit) and are not silently
folded in here — scope is a decision, per [AGENTS.md](../../../AGENTS.md) §2.

## Acceptance criteria

- [x] `docs/architecture/adr/adr-0012-*.md` exists, `Accepted`, and states: the two tiers
      and which sources reach each; `DeviceIdentity`/`StorageExtents` as the seam;
      refuse-on-unresolvable as the failure mode; and the container cases it does not
      catch (Storage Space, mounted VHD, loop-mounted image).
- [x] ADR-0012 declares that it supersedes ADR-0011's Validated half, naming it.
- [x] ADR-0011 carries a `Superseded by ADR-0012` marker on that half; its original text
      is otherwise unchanged.
- [x] ADR-0005's Consequences read exactly as `5079837` accepted them — verified by
      `git show 5079837:docs/architecture/adr/adr-0005-read-only-by-default.md`, not by eye.
- [x] No `Accepted` ADR contains a statement about the destination rule that contradicts
      `src/recovery/DestinationRule.cpp`.
- [x] The ADR index lists ADR-0012 and shows ADR-0011's new status.
- [x] Every code citation added by this story names a symbol, not a line number.

## Test plan

This story changes no code, so its verification is documentary and mechanical rather than
a unit test. Three checks, each of which must be able to fail:

- **The restore is exact.** A `git diff` between ADR-0005's Consequences at `5079837` and
  at this branch's head is empty. Demonstrated to fail by checking the diff is *non*-empty
  at the parent commit.
- **The superseded marker is real, and reachable before the stale text.** ADR-0011's two
  false bullets are *kept* — they are the record of what was believed on 2026-07-31 — so
  the check is not that they are gone. It is that a reader meets the marker first: the
  supersession notice precedes them, names ADR-0012, and the same is true of the third
  Consequence. [story-0705](story-0705-adr-immutability-check.md)'s check is the permanent
  form of this.
- **The claims in ADR-0012 match the code.** Each factual sentence about the rule is
  traced to the symbol it describes, and the trace is recorded in this story on
  completion. This is the check that would have caught all three defects above, and it is
  the one no script can do.

Not automated, and stated rather than hidden: nothing here prevents the *next* ADR from
describing code it never read. [story-0705](story-0705-adr-immutability-check.md) catches
the edit, not the inaccuracy. Accuracy stays a review obligation, and
[code-quality.md](../../code-quality.md) is where that obligation lives.

## Verified on completion (2026-08-06)

**The restore was checked mechanically, not by eye**, which the acceptance criteria
demanded because eye-checking is how the original edit survived review:

```bash
diff <(git show 5079837:docs/architecture/adr/adr-0005-read-only-by-default.md \
        | sed -n '/## Consequences/,$p') \
     <(sed -n '/## Consequences/,$p' docs/architecture/adr/adr-0005-read-only-by-default.md)
```

Empty. The same command against the parent commit reports the fourteen replaced lines, so
the check is not vacuous.

**Every factual sentence in ADR-0012 traced to the symbol it describes** — the check no
script can do, and the one that would have caught all three original defects:

| ADR-0012 says | Traced to |
|---|---|
| the rule lives outside `RecoverySink` | `recovery::destinationOnSource`, `recovery/DestinationRule.cpp` |
| tier one is spelling, every source | `startsPath(where, resolved(source))` before the source-kind test |
| tier two is device sources only | the `classifySource(source) != SourceKind::kDevice` early return |
| the seam is storage extents | `storageOf` / `storageUnder` → `overlaps`, `core/io/DeviceIdentity.hpp` |
| a whole disk covers every byte | `kWholeDisk` |
| unresolvable refuses | `refuseOverlap`'s `!hasValue()` branch |
| empty extents are a real answer | `StorageExtents`' own comment; empty is not an error |
| both tiers judge the same place | `resolved()` via `weakly_canonical`, applied to both |

**One imprecision left standing on purpose.** ADR-0005's restored text says the destination
must be "on a different volume", and the rule is narrower than that: a whole-disk source
rules out *every* volume on the disk, so a different volume is not always allowed. Editing
that sentence is exactly the move this story exists to undo, so it stays as accepted, and
ADR-0012 — which declares itself the implementation of ADR-0005's requirement — is where
the precise statement now lives. The ADR index points a reader from one to the other.

**Not fixed here, and still open:** ADR-0007 claims the CLI "warns about unreliable
destination storage", which nothing in the tree appears to do, and ADR-0008 names the
bad-sector map as durable session state the checkpoint does not persist. Both are the same
*kind* of defect as the three above and both were left out of scope deliberately (see
Design decisions); they remain recorded in
[epic-m6](../epic-m6-loose-ends.md#milestone-architecture-audit). Neither would be caught
by [story-0705](story-0705-adr-immutability-check.md)'s gate, since neither ADR was edited.

## Definition of Done

- [x] Acceptance criteria met, tests green (ASan + UBSan) — unchanged by this story, so
      the gate run proves only that nothing regressed.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
- [x] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
- [x] Docs/ADRs updated — this story *is* that item.
