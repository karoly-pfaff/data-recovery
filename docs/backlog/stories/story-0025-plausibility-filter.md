<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0025: Plausibility filter + format allowlist

- Epic: [epic-m2-carving-breadth](../epic-m2-carving-breadth.md)
- Status: Done
- Size: S

## Goal

Close M2 by making a wide carver set usable: a format allowlist so a scan only
pays for what the user asked for, and a plausibility filter so a structurally
valid but absurdly small match is not reported as a recovered file.

## Design references

- [ADR-0003](../../architecture/adr/adr-0003-validating-carving.md) — structure
  decides the extent.
- [ADR-0006](../../architecture/adr/adr-0006-candidate-arbitration-deferred-extraction.md)
  — the scanner reports verdicts; it never extracts.

## The two halves

**Format allowlist.** `registerBuiltinCarvers` takes an optional set of
extensions. With six carvers registered, every window is searched for every
signature; a user recovering photos should not pay for PDF and ZIP matching.
The allowlist is applied at registration, so an excluded format costs nothing
at all — not a signature search, not a carve attempt.

**Plausibility filter.** Structure alone cannot tell a real file from a
coincidence: a 22-byte byte sequence can be a structurally perfect JPEG, and a
disk full of random data will produce some. Each format therefore declares the
smallest size a real file of its kind can plausibly have. A candidate whose
extent falls below it is reported as `Rejected` rather than as a file — the
verdict is downgraded, never silently dropped, because deciding what to do with
a weak candidate belongs to arbitration (ADR-0006), not here.

This is deliberately *not* a content heuristic (entropy, magic-density,
whatever). It is a floor: everything above it is still decided by structure.

## Acceptance criteria

- [x] `plausibleMinimumBytes(extension)` gives each shipped format's floor.
      A format this build carries no carver for gets **no** floor: any number
      chosen for it would be invention rather than a format fact.
- [x] `applyPlausibility(CarveResult)` downgrades a below-floor result to
      `Rejected` with a zero extent, and leaves everything else untouched.
- [x] A `Rejected` result stays `Rejected`; the filter never upgrades.
- [x] The scanner applies the filter to every carve result before reporting.
- [x] `registerBuiltinCarvers(registry, allowlist)` registers only the named
      extensions; the existing one-argument form still registers everything.
- [x] An empty allowlist registers everything, so "no filter" is the default
      rather than "nothing works".

## Test plan

Unit (`tests/unit/carve/PlausibilityTest.cpp`):

- a below-floor Valid JPEG → `Rejected`, length 0;
- an at-floor result → untouched;
- an Uncertain result below the floor → `Rejected`;
- an already-`Rejected` result → unchanged;
- an unknown extension → the default floor applies.

Unit (`tests/unit/carve/CarverRegistryTest.cpp`): an allowlist of one extension
registers one carver; an empty allowlist registers all of them.

Integration (`tests/integration/JpegCarveGoldenTest.cpp`): the golden fixture
was a 22-byte structural JPEG — below any plausible floor, which is precisely
what the filter now rejects. It grows to a realistic size instead of the filter
being made configurable to accommodate it: a golden test whose fixture no real
scan would accept was testing the wrong thing.

## Definition of Done

- [x] Acceptance criteria met, tests green (ASan+UBSan).
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` + `docs/architecture/carving-engine.md` updated.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed.
- [x] Epic row linked.
