<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0705: An Accepted ADR cannot be edited — the immutability rule becomes a check

- Epic: [epic-m7-hardening](../epic-m7-hardening.md)
- Status: Ready
- Size: S

## Goal

ADR-0001 and the ADR index both state that an Accepted ADR is immutable and that a later
record supersedes it rather than editing it. Nothing enforces that, and M6 broke it in the
same commit range that documented it. Turn the rule into a red build.

## Design references

- [ADR-0001](../../architecture/adr/adr-0001-record-architecture-decisions.md) — *"ADRs
  are immutable once Accepted; a later ADR supersedes an earlier one rather than editing
  it."*
- [the ADR index](../../architecture/adr/README.md) — *"Superseding a decision means a new
  record, not an edit to the old one."*
- [story-0701](story-0701-adr-0012-destination-rule.md) — the breach this check would have
  caught, and **the story this one must land after**; see Ordering.
- [`tools/lint/`](../../../tools/lint/) — where the script goes, and the shape every gate
  here takes: a pure function over structured input, unit-tested from fixtures.
- [story-0704](story-0704-vacuity-refusal-in-gate-files.md) — the vacuity rule this gate
  is subject to like any other.

## The breach it is written from

`4a4221e` wrote the two-tier destination rule into ADR-0005's Consequences **in place**
(+14/−2), while ADR-0005 was `Accepted`. The same increment added the ADR index that
restates the rule. That is the audit's highest-severity finding, and it is entirely
mechanical to catch.

## Design decisions

**The check is over a diff, not over the tree.** Immutability is a statement about
*change*, and no snapshot of the files can express it. The script takes a revision range,
asks `git diff` which ADR files changed and in which hunks, and judges.

**Only Decision and Consequences are frozen.** Status must change — that is how
superseding is recorded. Dates, typo fixes in Context, a corrected link: none of those are
the decision. Freezing the whole file would make the rule unusable and would train people
to bypass the gate, which is worse than not having it. The script parses the ADR's section
headings and judges hunks by which section they fall in.

**Two escapes, both of which must be visible in the same change:**

1. the same diff adds a new `adr-NNNN-*.md`, or
2. the same diff marks the edited ADR `Superseded`.

Either way the edit leaves a trace, which is the entire point of the rule. An edit with
neither is a failure.

**Restoring an unauthorised edit is the third case, and it is why this story lands after
story-0701.** story-0701 puts ADR-0005's Consequences back to the text `5079837` accepted.
That is an edit to an Accepted ADR's Consequences with no new ADR marking *ADR-0005*
superseded — so this gate, as specified, would refuse it. Three ways out were considered:

| Option | Verdict |
|--------|---------|
| Detect "this hunk reverts to an earlier committed state" | Rejected. Real, but it is a second diff-archaeology feature to build and test for one event, and it would also permit reverting a *legitimate* supersession. |
| A `--allow` escape flag on the script | Rejected. An escape hatch used once becomes the way the gate is answered. |
| **Order the stories: 0701 first, 0705 second** | **Taken.** The restore lands before the gate exists; the gate then holds from a correct baseline. Costs nothing but a sequencing note. |

This is a genuine dependency and the epic's "the other five block nothing and can land in
any order" does not hold for this pair. It is recorded in the epic.

**The range is supplied, not guessed.** In CI it is the PR's merge base to head; locally
it defaults to `main...HEAD`. The script does not try to infer "the current PR" from the
environment — a gate that guesses its own input is how you get one that inspects nothing,
which is [story-0704](story-0704-vacuity-refusal-in-gate-files.md)'s whole subject. A
range naming no commits is a configuration fault and exits 2.

**What it cannot do, stated up front.** It catches an *edit*, not an *inaccuracy*. An ADR
that was wrong the day it was written passes forever, and both of the epic's leftover
observations — ADR-0008 on the bad-sector map, ADR-0007 on the composed decorators — are
exactly that shape, since neither ADR was ever edited. This gate would have caught neither.
Accuracy stays a review obligation and belongs to the milestone audit.

## Acceptance criteria

- [ ] `tools/lint/check_adr_immutability.py` takes a revision range and exits non-zero when
      a hunk inside the Decision or Consequences of an `Accepted` ADR changed.
- [ ] It exits zero when the same diff adds a new ADR file.
- [ ] It exits zero when the same diff marks the edited ADR `Superseded`.
- [ ] It exits zero for changes to Status, Date, Context, or a non-ADR file.
- [ ] It exits 2 — not 0 — for a range naming no commits, and for a range it cannot parse.
- [ ] Run over `4a4221e^..4a4221e`, it **fails and names ADR-0005** — the historical breach
      is the acceptance fixture, not a synthetic one.
- [ ] Run over `main` at this branch's merge base, it passes.
- [ ] CI runs it on pull requests with the PR's own range.
- [ ] `docs/testing/quality-gates.md` documents it and says what it cannot catch.

## Test plan

Unit (`tests/unit/lint/test_check_adr_immutability.py`), over fixture diffs rather than
the live repository, so the cases are stable:

- an Accepted ADR's Consequences edited, nothing else → fails, names the ADR and section
- the same edit plus a new `adr-NNNN-*.md` in the diff → passes
- the same edit plus the ADR's Status becoming `Superseded` → passes
- a `Proposed` ADR's Decision edited → passes (only Accepted is frozen)
- Status/Date-only change → passes
- Context edited → passes
- an empty range → exits 2

Integration, and the one that matters: the real `4a4221e^..4a4221e` range fails. That is
the test which proves the gate catches the thing it was written for, and per
[code-quality.md](../../code-quality.md) the gate is unverified until it has been watched
failing on it.

## Ordering

**Lands after [story-0701](story-0701-adr-0012-destination-rule.md).** See Design
decisions. No other story in M7 constrains it.

## Definition of Done

- [ ] Acceptance criteria met, tests green (ASan + UBSan).
- [ ] clang-format, clang-tidy, duplication, file-length guard clean.
- [ ] `CHANGELOG.md` updated under `[Unreleased]`.
- [ ] Epic row linked.
- [ ] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
- [ ] `docs/testing/quality-gates.md` updated.
