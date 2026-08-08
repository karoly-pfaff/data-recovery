<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0707: A source whose identity cannot be resolved is a decision, not a dead end

- Epic: [epic-m7-hardening](../epic-m7-hardening.md)
- Status: Done
- Size: M

## Goal

The destination rule refuses when it cannot resolve either side's physical identity. A
VeraCrypt volume has no resolvable identity, so **Revenant cannot be run against an
encrypted volume at all** — not degraded, not warned, refused. Let the operator take that
decision, without ever letting them take the other one.

## Design references

- [`src/recovery/DestinationRule.cpp`](../../../src/recovery/DestinationRule.cpp) —
  `refuseOverlap`, where both refusals live, and `destinationOnSource`, the whole rule.
- [`include/revenant/core/io/DeviceIdentity.hpp`](../../../include/revenant/core/io/DeviceIdentity.hpp)
  — `storageOf` / `storageUnder` return `Result<StorageExtents>`; an empty vector is a real
  answer (a network share), an error is the unanswerable one.
- [story-0609](story-0609-destination-on-source-refused.md) — built the rule. The
  containers it is too *permissive* about are listed in
  [ADR-0012](../../architecture/adr/adr-0012-destination-rule-two-tiers.md), which owns
  that list; story-0609 itself records none of them, and earlier drafts of this story and
  of the epic wrongly credited it. This story is the same blind spot with the opposite sign.
- [story-0701](story-0701-adr-0012-destination-rule.md) — ADR-0012 records the rule; if
  0701 has landed, ADR-0012 gains this flag's exception. Not a blocking dependency.
- [`include/revenant/recovery/Manifest.hpp`](../../../include/revenant/recovery/Manifest.hpp)
  — `SessionManifest`, where the run-level fact is recorded.
- [ADR-0005](../../architecture/adr/adr-0005-read-only-by-default.md) — the guarantee this
  must not weaken.

## What is actually happening

Found by pointing the shipped `v0.4.0` binaries at a VeraCrypt-unlocked external disk on
2026-08-04 — the first time this tool met storage nobody had built a fixture for.

`refuseOverlap` compares two resolved identities — `storageOf(source)` and
`storageUnder(destination)` — and refuses when *either* fails. For a VeraCrypt volume
Windows maps no partition or disk: it is a virtual device with nothing behind it, and the
question the rule asks has no answer.

**Which side fails decides how bad it is, and the two must not be conflated.** With a
VeraCrypt volume as the **source**, `storageOf` → `storageOfDevicePath` fails, so
`refuseOverlap` takes its first branch whatever the destination is, and **every**
destination is refused — the run cannot start at all. That is the case that makes an
encrypted drive unrecoverable, and it is this story's subject. A VeraCrypt
**destination** fails on the `storageUnder` side and refuses only *that* destination,
which is a much smaller problem: the operator picks somewhere else.

*(The first drafts of this story and of the epic attributed the total refusal to
`storageUnder`, which names the destination side and cannot produce it. Caught by
story-0701's self-audit before anything was built on it.)*

Refusing on an unanswerable question is the right default — story-0609 chose it
deliberately, and "assume elsewhere" is how you overwrite the clusters you are reading.
The defect is that it is the *only* option.

## The one sentence the design reduces to

**It may relax the unresolvable case and must never touch the proven-overlap case.** If
the tool can show the destination sits on the source, nothing overrides that. If it cannot
tell, the operator may state that they checked.

## Design decisions

**The two refusals must be separated in the code before a flag can be attached to one.**
Today `refuseOverlap` returns the same `ErrorCode::kDestinationOnSource` for both, and its
own comment says why: *"One code covers both the proven conflict and the unanswerable
question: the operator's next step is the same either way."* That was correct when the
next step *was* the same. It no longer is. The function must distinguish, internally,
"proven to overlap" from "could not be answered" — this is the load-bearing change, and
every safety property below rests on it.

**What the operator sees stays two codes, not one.** The reported `ErrorCode` for a proven
overlap is unchanged. The unresolvable case gets its own code, so the message can say what
to do about it — which is the whole reason the merged code was wrong.

**The flag affirms, it does not disable.** `--allow-unverified-destination`. Long and
awkward on purpose: it should be typed by someone who decided, not by someone clearing an
obstacle. It does not take a value, matching every other boolean flag here.

**It is inert on the proven branch, and a test enforces that.** This is where the story's
whole safety value sits, so it is not left to the implementation's good intentions: a test
constructs a source and destination with *proven* overlapping extents, passes the flag,
and asserts the run is still refused. If that test does not exist, the story is not done.

**The spelling tier is untouched.** `destinationOnSource` refuses with `kInvalidArgument`
when the output tree would grow inside the source path, and that has nothing to do with
identity resolution. The flag must not reach it either. Also tested.

**The manifest records that the run started on an unverified identity.** A recovery whose
safety rests on an operator's assertion rather than on a check must carry that fact into
its own record — otherwise the manifest of an unverified run is indistinguishable from a
verified one, six months later, to someone else. It is a run-level boolean on
`SessionManifest`, present in the JSON on every run so its absence cannot be mistaken for
false.

**The refusal message names the flag; it does not suggest it.** An error that says "pass
`--allow-unverified-destination` to continue" reads as an instruction. It says the identity
could not be resolved, that the tool cannot confirm the destination is off the source, and
that the flag exists for an operator who has confirmed it themselves. The difference is
whether a frightened user hits the flag reflexively.

**Not in scope: resolving VeraCrypt's identity properly.** Asking VeraCrypt which physical
disk backs a mounted volume would turn the unanswerable question into an answerable one
for this container, and is a much larger piece of work with a per-container answer.
The containers story-0609 was too permissive about — listed in
[ADR-0012](../../architecture/adr/adr-0012-destination-rule-two-tiers.md), which owns that
list — are the same family. This story makes the *general* unresolvable case survivable; it
does not enumerate containers.

## Acceptance criteria

- [x] `refuseOverlap` distinguishes "proven overlap" from "identity unresolvable"
      internally.
- [x] The two report different `ErrorCode`s; the proven-overlap code is unchanged from
      today.
- [x] `--allow-unverified-destination` is accepted by both recovery frontends, takes no
      value, and is refused when stated twice (matching every other boolean flag).
- [x] With the flag, a run whose identities cannot be resolved **proceeds**.
- [x] With the flag, a run with proven overlapping extents is **still refused**.
- [x] With the flag, a destination inside the source path is **still refused** by the
      spelling tier.
- [x] Without the flag, behaviour is byte-for-byte what it is today.
- [x] `SessionManifest` carries a run-level field recording whether the run started on an
      unverified identity, emitted on every run.
- [x] The refusal message names the condition and the flag without instructing the user to
      pass it.
- [x] `--help` lists the flag ([story-0702](story-0702-one-flag-table.md) owns the
      table; if 0702 has landed, the flag is added there rather than to a help string).
- [x] `docs/usage.md` documents it, including what it does not relax.

## Test plan

Unit (`tests/unit/recovery/`), over `refuseOverlap` directly, which is where the safety
property lives:

- `unresolvable_source_is_refused_without_the_flag` — today's behaviour, pinned.
- `unresolvable_destination_is_refused_without_the_flag` — the other side.
- `unresolvable_is_allowed_with_the_flag` — the feature.
- **`proven_overlap_is_refused_with_the_flag`** — the test the story exists for. Both
  identities resolve, the extents overlap, the flag is set: still refused.
- `proven_disjoint_is_allowed_either_way` — the flag changes nothing it should not.
- `an_empty_extent_list_is_not_an_unresolved_identity` — a network share resolves to no
  local storage, which is a real answer; the flag must not be needed for it, and must not
  change it. This is the case most likely to be conflated with the unresolvable one.

Unit (`tests/unit/cli/`): the flag parses, is rejected when repeated, and is absent by
default; the manifest field follows it.

Integration: a run over an image fixture with the flag set produces a manifest whose
unverified-identity field is true, and one without it produces false.

Not automated, recorded on completion: a manual pass against the VeraCrypt volume that
found this, confirming a real recovery now starts. It needs hardware CI does not have —
the same position [story-0603](story-0603-linux-loop-device.md) was in, and it is recorded
as a transcript rather than gated.

## Verified on completion (2026-08-08)

**The load-bearing change was the first one, exactly as the story said.** `refuseOverlap`
returned one error code for the proven conflict and the unanswerable question, and its own
comment said why — *"the operator's next step is the same either way"*. That was true when
it was written and stopped being true the moment an encrypted volume was pointed at. Until
the two were told apart, there was nothing to attach a flag to.

**The test the story exists for is `StillRefusesAProvenOverlapWhenTheOperatorSaysSo`.** Both
identities resolve, the extents overlap, the flag is set: still refused. The whole safety
value of the flag is in that separation, so it is asserted rather than intended. Two more
guard the edges: the spelling tier refuses a destination whose tree would grow around the
source whatever the operator says, and an *empty* extent list — a network share, which
resolves to no local storage — is a real answer that the flag is neither needed for nor able
to change. That last one is the case most easily conflated with the unresolvable one.

**`UnverifiedIdentity` is a named type, not a `bool`.** It passes through three signatures,
and at each of them the question is which of two refusals it may touch. A bare boolean would
have read as "skip the check".

**It needed a public header.** `RecoverySink::open` takes it and `RecoverySink.hpp` is in the
public tree, so the enum could not live beside the internal rule that consumes it —
`include/` is a 1.0 compatibility promise, and a public signature naming an internal type
would have broken that quietly.

**ADR-0013 rather than an edit to ADR-0012.** ADR-0012 is `Accepted`, and story-0705's gate
now refuses an in-place edit to a frozen section — which is the rule ADR-0001 has always
stated and nothing enforced. The story's own Definition of Done said "ADR-0012 records the
flag as the exception"; that was written before the gate existed and would now be refused by
it. Letting an operator override the identity check is a change to the destination rule, so
a superseding record is the right shape and not merely the permitted one.

**Not automated, and not claimed:** the manual pass against the VeraCrypt volume that found
this. It needs hardware CI does not have — the same position story-0603 was in. What *is*
pinned is every branch of the rule, over synthetic identities.

## Definition of Done

- [x] Acceptance criteria met, tests green (ASan + UBSan).
- [x] Coverage held or raised (≥ 85% core).
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
- [x] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
- [x] ADR-0012 (if [story-0701](story-0701-adr-0012-destination-rule.md) has landed) records
      the flag as the exception to refuse-on-unresolvable; `docs/usage.md` updated.
