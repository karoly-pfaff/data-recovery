<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# ADR-0013: An unresolvable identity is a decision, not a dead end

- **Status:** Accepted
- **Date:** 2026-08-08
- **Supersedes:** [ADR-0012](adr-0012-destination-rule-two-tiers.md)

## Context

[ADR-0012](adr-0012-destination-rule-two-tiers.md) records the destination rule
in two tiers: a spelling tier for every source, then a physical-identity
comparison over storage extents for device sources, refusing when either side's
identity cannot be resolved. Refusing on an unanswerable question was chosen
deliberately — "assume elsewhere" is how you overwrite the clusters you are
reading — and that choice stands.

What it did not anticipate is a source that *never* has an identity. Pointing
the shipped `v0.4.0` binaries at a VeraCrypt-unlocked external disk on
2026-08-04 — the first time this tool met storage nobody had built a fixture for
— found that Windows maps no partition and no disk behind such a volume. That is
precisely the question the second tier asks, so `storageOf` fails, the first
branch of the refusal is taken whatever the destination is, and **every**
destination is refused. Revenant cannot be run against an encrypted volume at
all: not degraded, not warned, refused.

Which side fails decides how bad it is, and the two must not be conflated. A
VeraCrypt volume as the **source** is the case above. A VeraCrypt
**destination** fails on the `storageUnder` side and costs only that one
destination — the operator picks somewhere else.

The containers ADR-0012 lists as ones the rule cannot see through are the same
blind spot with the opposite sign: cases where the rule is too *permissive*.
This one makes an ordinary recovery scenario impossible.

## Decision

**The two refusals become two refusals.** `refuseOverlap` reported one error
code for both the proven conflict and the unanswerable question, and its own
comment said why: the operator's next step was the same either way. It no longer
is. A proven overlap keeps `kDestinationOnSource`; an identity that could not be
resolved reports `kDestinationIdentityUnresolved`. This separation is the
load-bearing change and every safety property below rests on it.

**The operator may answer the unanswerable question, and only that one.**
`--allow-unverified-destination` relaxes the unresolvable case. It **must never
touch the proven-overlap case**: if the tool can show the destination sits on
the source, an operator's assertion that they checked is simply wrong. It does
not reach the spelling tier either — a destination whose output tree would grow
around the source is refused from spelling alone, and nothing about that is
unresolvable.

**The flag affirms; it does not disable.** The name is long and awkward on
purpose: it should be typed by someone who decided, not by someone clearing an
obstacle. It is carried as a named type rather than a bare `bool`, so the
separation is visible at every signature it passes through.

**The manifest records that the run rested on an assertion.** A recovery whose
safety rests on an operator's word rather than on a check carries that fact in
its own record, as a run-level boolean present on every run — so the manifest of
an unverified run is not distinguishable from a verified one only by a missing
field, six months later, to someone else.

## Consequences

The two tiers of ADR-0012 are unchanged in what they compare and when they run.
What changes is that the identity tier now has three answers rather than two:
proven-overlap, proven-disjoint, and cannot-tell — with the last one being a
question the operator can settle rather than a wall.

An encrypted volume can be recovered from. That is the point.

The gate that refuses an in-place edit to an `Accepted` ADR (story-0705) is why
this is a new record rather than an amendment to ADR-0012, and it is the rule
ADR-0001 has always stated. ADR-0012 remains the account of the two tiers and of
the containers the rule cannot see through; this record changes only what
happens when the second tier cannot answer.

**What this does not do:** it does not resolve VeraCrypt's identity. Asking
VeraCrypt which physical disk backs a mounted volume would turn the unanswerable
question into an answerable one for that container, and is a much larger piece of
work with a per-container answer. This makes the *general* unresolvable case
survivable; it does not enumerate containers.
