<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0701: ADR-0012 records the two-tier destination rule, and ADR-0005 becomes immutable again

- Epic: [epic-m7-hardening](../epic-m7-hardening.md)
- Status: Done
- Size: S

## Goal

An `Accepted` ADR describes the destination check as the exact bug M6 fixed, and the
decision that replaced it was written into a *different* ADR's Consequences in place,
which [ADR-0001](../../architecture/adr/adr-0001-record-architecture-decisions.md)
forbids. Give the real rule an immutable record of its own, and put back the text that
was overwritten.

## Design references

- [`docs/architecture/adr/adr-0011-two-halves-of-the-read-only-guarantee.md`](../../architecture/adr/adr-0011-two-halves-of-the-read-only-guarantee.md)
  — `Accepted`, dated 2026-07-31. Its "Validated" half and its **second and third**
  Consequences are the false text.
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

Four separate defects, all in the record rather than the code:

| # | Where | What it says | What is true |
|:-:|-------|--------------|--------------|
| 1 | ADR-0011, "Validated" half | the rule "is enforced by a lexical path-prefix comparison in `RecoverySink`" | it lives in `recovery/DestinationRule`, and spelling is only the first of two tiers |
| 2 | ADR-0011, same half | "That comparison does not hold for raw-device sources … story-0609 exists to make ADR-0005's sentence true" | story-0609 landed at `4a4221e`, inside M6 |
| 3 | ADR-0011, Consequences | output landing on the source is "aspiration, not mechanism", and a successor "should say so" once story-0609 lands | it is mechanism, and the successor is ADR-0012 rather than an edit here |
| 4 | ADR-0005, Consequences | the two-tier rule, written *in place* over the accepted text | an `Accepted` ADR's Decision and Consequences are immutable |

ADR-0011 instructed its own successor — "When story-0609 lands, the Validated half
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
The containers the rule cannot see through are enumerated in ADR-0012 and nowhere else —
this story moved the other statements of that list to defer to it. An ADR that records only
the cases a rule handles is how the next reader learns the wrong boundary.

**Not in scope: the three other ADR-versus-code claims.** The epic's notes flag ADR-0008
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
- [x] ADR-0012 matches `src/recovery/DestinationRule.cpp`, and every *other* statement
      about which destinations the rule refuses, surviving in an `Accepted` ADR, is
      reachable only through one of exactly three routes, each named here:
      1. it matches the code;
      2. it carries a supersession marker naming ADR-0012, placed before the stale text —
         ADR-0011's *Validated* half and its second and third Consequences;
      3. **it is ADR-0005's Consequence, which matches neither and carries no marker.**
         "On a different volume" is narrower than the rule, and marking or editing it is
         the move this story exists to undo. It is reached instead through the ADR index,
         which names ADR-0012 as the record of this rule, and through ADR-0012's own
         `Implements: ADR-0005` header. This is a knowingly accepted exception, not a
         satisfied condition.

      *(Third wording. The first was absolute and false. The second was softer and still
      false — it admitted only routes 1 and 2 while the story relied on route 3 in a
      paragraph eighty lines below the box. An exception that lives outside the criterion
      is exactly what the first version was rejected for.)*
- [x] The ADR index lists ADR-0012 and shows ADR-0011's new status.
- [x] Every code citation added by this story names a symbol, not a line number.

## Test plan

This story changes no code, so its verification is documentary and mechanical rather than
a unit test. Three checks, each of which must be able to fail:

- **The restore is exact.** A `git diff` between ADR-0005's Consequences at `5079837` and
  at this branch's head is empty. Demonstrated to fail by checking the diff is *non*-empty
  at the parent commit.
- **The superseded marker is real, and reachable before the stale text.** ADR-0011's false
  text is *kept* — it is the record of what was believed on 2026-07-31 — so the check is
  not that it is gone. It is that a reader meets the marker first, in **both** places: the
  notice precedes the *Validated* half's two bullets, and a second notice opens the
  Consequences naming the second and third bullets below it.
  [story-0705](story-0705-adr-immutability-check.md)'s check is the permanent form of this.
  *(The Consequences marker was placed after its bullets in the first attempt, while this
  very sentence claimed it came first — found by the self-audit, and exactly the failure
  the story is about: a check the artifact does not satisfy is not a check.)*
- **The claims in ADR-0012 match the code.** Each factual sentence about the rule is
  traced to the symbol it describes, and the trace is recorded in this story on
  completion. This is the check that would have caught all four defects above, and it is
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
script can do, and the one that would have caught all four original defects:

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
| a path naming nothing classifies as a device | `classifySource`'s final ternary; `SourceDevice.hpp`'s own note; `ClassifiesWhatIsNeitherFileNorFolderAsADevice` pins it |
| the rule runs before the first read | `RecoveryRun`'s call site precedes the first device read |
| Storage Space / mounted VHD report the virtual disk | `VolumeExtentsWindows.cpp` — the extents reply the OS gives a volume |
| a Linux loop-mounted image reports as its own disk | `SysfsWalk.cpp`'s resolution of a device to the disks under it |
| a VeraCrypt **source** makes every destination refused | `storageOf` → `storageOfDevicePath` fails for it; `refuseOverlap`'s `!hasValue()` branch then refuses whatever the destination is. **Source side, not `storageUnder`** — a destination-side failure refuses only that destination |
| story-0609 landed and replaced the path-only rule | commit `4a4221e` |

**This table has been wrong three times, in the story whose subject is claims nobody
re-checked.** Round one showed eight rows under the heading "every factual sentence". Round
two added four that did not match the four the prose claimed — one described `slaves`
tracing, which lives in `recovery-output.md` and **not in ADR-0012 at all**, while the
VeraCrypt consequence still had no row and was named as covered. Round three fixed every
*left* cell and left a wrong *right* cell: the VeraCrypt row named `storageUnder`, the
destination-side call, for a sentence that only follows from the **source** side failing.
Each failure was found by the self-audit, never by the table's author, and the third was
the row added to repair the second.

The row was copied from this story's own prose rather than re-derived from the code — and
that prose came from the epic, which got it wrong first. `storageOf` and `storageUnder` are
the two arguments of one call; naming the wrong one inverts what is refused, from "this run
cannot start" to "this destination is unavailable". Both `story-0707` and the epic carried
the same inversion and are corrected with it, before story-0707 is built on it.

One row is honest about not tracing to a symbol: the story-0609 history traces to commit
`4a4221e`, because a landed change is a commit and not a symbol. The heading above says
"traced to the symbol it describes"; that row is the stated exception rather than a silent
one.

**One imprecision left standing on purpose** — ADR-0005's "on a different volume", narrower
in the rule than in the sentence. It is acceptance criterion 5's route 3 above, stated
there rather than restated here.

**Not fixed here, and still open.** Three ADR-versus-code claims, and they do *not* all have
the same status — the first version of this paragraph deferred all of them "as recorded in
epic-m6", and one of them was recorded nowhere:

| Claim | Status |
|---|---|
| ADR-0008 names the bad-sector map as durable session state the checkpoint does not persist | recorded by the M6 audit in [epic-m6](../epic-m6-loose-ends.md#milestone-architecture-audit) |
| ADR-0007 justifies network sources by decorators `SourceStack::over` no longer composes | recorded in the same place; named in this story's Design decisions |
| **ADR-0007 says the CLI "warns about unreliable destination storage"** — the only warning in the tree is `RunSummary`'s, about volume metadata | **found by this story, recorded nowhere before now.** It is written into [epic-m7's notes](../epic-m7-hardening.md#notes) by this change, because a finding deferred to a paragraph about something else is a finding lost |

None would be caught by [story-0705](story-0705-adr-immutability-check.md)'s gate: it
catches an edit, and none of these ADRs was edited.

## Definition of Done

- [x] Acceptance criteria met, tests green (ASan + UBSan) — unchanged by this story, so
      the gate run proves only that nothing regressed.
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
- [x] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
- [x] Docs/ADRs updated — this story *is* that item.
