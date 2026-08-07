<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0705: An Accepted ADR cannot be edited — the immutability rule becomes a check

- Epic: [epic-m7-hardening](../epic-m7-hardening.md)
- Status: In review
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

**Two escapes, both of which must be visible in the same change, and both of which name
the ADR being edited:**

1. the same diff adds a new `adr-NNNN-*.md` **that names the edited ADR as the one it
   supersedes**, or
2. the same diff marks **the edited ADR** `Superseded`.

Either way the edit leaves a trace pointing at *this* record, which is the entire point of
the rule. An edit with neither is a failure.

**"Any new ADR in the diff" is not enough, and the difference is not academic.** The loose
reading — a new ADR anywhere in the change permits any edit anywhere — would let a change
that legitimately adds one record quietly rewrite an unrelated one, which is a superset of
the breach this gate exists to catch. It would also have let
[story-0701](story-0701-adr-0012-destination-rule.md) through on a technicality: that diff
adds ADR-0012 and edits ADR-0005, but ADR-0012 supersedes ADR-0011's Validated half, **not
ADR-0005**. Under the loose reading the restore passes for the wrong reason; under the
strict one it is correctly refused, and the ordering below is what resolves it. A gate that
passes the right change for the wrong reason is the failure mode this milestone exists to
remove.

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

- [x] `tools/lint/check_adr_immutability.py` takes a revision range and exits non-zero when
      a hunk inside the Decision or Consequences of an `Accepted` ADR changed.
- [x] It exits zero when the same diff adds a new ADR that names the edited ADR as
      superseded.
- [x] It exits **non-zero** when the diff adds a new ADR that names a *different* ADR —
      the loose reading, explicitly refused.
- [x] It exits zero when the same diff marks the edited ADR `Superseded`.
- [x] It exits zero for changes to Status, Date, Context, or a non-ADR file.
- [x] It exits 2 — not 0 — for a range naming no commits, and for a range it cannot parse.
- [x] Run over `4a4221e^..4a4221e`, it **fails and names ADR-0005** — the historical breach
      is the acceptance fixture, not a synthetic one.
- [x] Run over `main` at this branch's merge base, it passes.
- [x] CI runs it on pull requests with the PR's own range.
- [x] `docs/testing/quality-gates.md` documents it and says what it cannot catch.

## Test plan

Unit (`tests/unit/lint/test_check_adr_immutability.py`), over fixture diffs rather than
the live repository, so the cases are stable:

- an Accepted ADR's Consequences edited, nothing else → fails, names the ADR and section
- the same edit plus a new ADR naming it as superseded → passes
- the same edit plus a new ADR naming a *different* ADR → **fails** (the loose reading)
- the same edit plus the ADR's Status becoming `Superseded` → passes
- a `Proposed` ADR's Decision edited → passes (only Accepted is frozen)
- Status/Date-only change → passes
- Context edited → passes
- an empty range → exits 2

One file per subject, split out as the first kept growing past the 250-line limit. Cases
that cost three commits through a repository belong in the ones over strings; a case that
is one line does not need a repository at all.

| File | Its subject |
|---|---|
| `test_check_adr_immutability.py` | what is frozen, and what excuses an edit |
| `test_adr_frozen_lines.py` | which lines *belong* to a frozen section — where the gate and git disagreed about what a line is |
| `test_adr_gate_ranges.py` | renames, removals and range syntax |
| `test_adr_gate_environment.py` | what the repository and the shell can do to git's answer: attributes, diff drivers, textconv, the working directory |
| `test_adr_gate_faults.py` | everything that must exit 2 rather than 0 or 1 |
| `test_adr_document.py` | fences, headings and Status, over strings |
| `test_adr_supersedes.py` | the `**Supersedes:**` clause and ADR paths, over strings |
| `test_adr_range.py` | the `--name-status` parse and `split_range` — the refusal for an unreadable line is reachable no other way |
| `adr_fixture.py` | the throwaway repository the gate-level files drive; not named `test_*`, so it is imported rather than collected |

Integration, and the one that matters: the real `4a4221e^..4a4221e` range fails. That is
the test which proves the gate catches the thing it was written for, and per
[code-quality.md](../../code-quality.md) the gate is unverified until it has been watched
failing on it.

## Ordering

**Lands after [story-0701](story-0701-adr-0012-destination-rule.md).** See Design
decisions. No other story in M7 constrains it.

## Verified on completion (2026-08-07)

**The acceptance fixture is the real breach, and it fails on it.** Over
`4a4221e^..4a4221e` the gate exits 1 with *"ADR-0005: Consequences was edited while the ADR
is Accepted"*. A gate written to catch one specific commit is unverified until it has been
run against that commit, so that is a test (`TheHistoricalBreach`) rather than a paragraph.

**The ordering constraint this story derived turned out to be real.** Over story-0701's
range, ADR-0011 is excused — ADR-0012 declares `**Supersedes:** … ADR-0011` — and
**ADR-0005's restore is refused**, because nothing declares ADR-0005 superseded. That is
what the story predicted when it argued for sequencing over an escape hatch. Unlike the
`4a4221e` case it is *not* pinned by a test: its range is a merge state rather than a
commit, so an assertion over it would break the next time `main` moved.

**Six audit rounds found nineteen ways to pass this gate while an Accepted ADR was
rewritten.** Every one was reproduced by running the gate before it was fixed, and every
fix is pinned by a test watched failing with the fix reverted. They fall into four classes,
and the classes are the transferable part:

| Class | What it looked like here |
|---|---|
| **The input can be emptied.** A gate is its input as much as its logic. | Run from `docs/` rather than the repository root, the relative pathspec matched nothing while the range stayed non-empty: *"no Accepted ADR was edited in place"*, exit 0, on the breach commit. One committed `.gitattributes` line marking the ADRs `-diff` does the same, as does `diff.external`, as does a `textconv` filter. Four separate channels, none exotic. |
| **Each side of a diff is blind where the other sees.** | A pure removal gains no new-side line, so deletions from a frozen section read as untouched. A pure insertion covers no old-side line — and worse, what it inserts moves the boundary it is judged against: `## Notes` beneath `## Decision` ends the Decision span at its own heading. |
| **The question was asked of the wrong side, or the wrong moment.** | "Was this Accepted?" asked of the post-image made demoting the Status in the same commit a general-purpose escape hatch. "Which record is this?" asked of the new name let `git mv` into a subdirectory erase it. "Is it well-formed?" asked on *addition* rather than on *arrival* let a draft land incomplete and then be promoted. |
| **The tool and its input disagreed about what a line is.** | `str.splitlines()` breaks on U+2028, U+2029, U+0085, `\v`, `\f` and `\x1c`-`\x1e`; git counts `\n`. One such character above a frozen heading shifted every span past the hunk numbers git reports, and the top of the Decision fell outside its own section. A form feed pasted from a word processor is enough. |

Two patterns are worth carrying to the next gate:

- **A fix that narrows an input set needs the same scrutiny as the defect it removes.**
  `--diff-filter=M` fixed a new ADR reporting itself as edited and lost renames and
  deletions. Blanking fenced examples fixed a false escape and made one unclosed fence
  blank the file. Each of the four rounds after the first was correct about the rule and
  wrong about the machinery underneath it.
- **Two commits are one change.** Three separate escapes worked by splitting a refused
  edit across two pull requests: mangle the Status header, then demote and rewrite; mark a
  record `Superseded`, then delete it; insert a heading, then rewrite beneath it. Each step
  was individually legitimate. Whenever a rule reads the pre-image, ask what a *previous*
  change could have made the pre-image say.

**What it cannot catch is unchanged and stated in the gate's own documentation:** an
*inaccuracy*. An ADR that was wrong the day it was written passes forever.

**One constraint contributors will meet.** A record is frozen once it is `Accepted`, and
stays frozen once it is `Superseded`. The pointer to a successor — the *"Superseded by
ADR-00NN"* note this repository writes inside Decision and Consequences, as ADR-0011 does —
must therefore go in **with** the change that supersedes the record, not afterwards. The
header bullets are never frozen, so a pointer there is always available.

## Definition of Done

- [x] Acceptance criteria met, tests green (ASan + UBSan).
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
- [x] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
- [x] `docs/testing/quality-gates.md` updated.
