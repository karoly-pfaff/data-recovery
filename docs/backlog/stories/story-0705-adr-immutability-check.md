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

Unit (`tests/unit/lint/test_adr_document.py`), over strings rather than repositories: the
fence, heading, Status and `**Supersedes:**` parsing, where a case is one line and four of
this gate's silent passes came from.

Unit (`tests/unit/lint/test_adr_gate_ranges.py`), split from the file above when it reached
457 lines: renames, removals, range syntax, and the reader's own git configuration — every
question about *what changed* rather than about *what is frozen*. `adr_fixture.py` holds the
throwaway repository both drive.

Integration, and the one that matters: the real `4a4221e^..4a4221e` range fails. That is
the test which proves the gate catches the thing it was written for, and per
[code-quality.md](../../code-quality.md) the gate is unverified until it has been watched
failing on it.

## Ordering

**Lands after [story-0701](story-0701-adr-0012-destination-rule.md).** See Design
decisions. No other story in M7 constrains it.

## Verified on completion (2026-08-07)

**The acceptance fixture is the real breach, and it fails on it.** Run over
`4a4221e^..4a4221e` the gate exits 1 with *"ADR-0005: Consequences was edited while the ADR
is Accepted"*. A gate written to catch one specific commit is unverified until it has been
run against that commit, so that assertion is a test rather than a paragraph
(`TheHistoricalBreach`).

**The ordering constraint this story derived turned out to be real, and the gate proves it.**
Run over story-0701's commit, ADR-0011 is excused — ADR-0012 declares `**Supersedes:** …
ADR-0011` — and **ADR-0005's restore is refused**, because nothing declares ADR-0005
superseded. That is exactly what the story predicted when it argued for sequencing over an
escape hatch, and it is why 0705 had to land after 0701. Unlike the `4a4221e` case this one
is *not* pinned by a test: its range is a merge state rather than a commit, so an assertion
over it would break the next time `main` moved. It was verified by running, and re-verified
after each round of fixes.

**Four defects found by running it, none visible in the code.**

| What | Why it mattered |
|---|---|
| A newly added ADR was flagged as edited | Every line of a new file is "changed", so ADR-0012 reported its own Decision and Consequences as rewritten — the one thing this gate must permit. `--diff-filter=M`. |
| A wrapped `**Supersedes:**` was missed | ADR-0012's header wraps before naming ADR-0011, so a same-line search found nothing and the gate flagged an ADR that *was* properly superseded. |
| A fixed character window then over-excused | Widening to 200 characters swallowed the *next* header bullet — `**Implements:** ADR-0005` — and excused the ADR-0005 edit on the strength of a sentence about ADR-0011. |
| Prose counted as a declaration | With the clause bounded, ADR-0012's Context still says "an Accepted ADR is superseded by a new record, not edited" two sentences from "ADR-0005". Only the header field counts now. |

The last two are the same defect the story warns about in the abstract — an escape loose
enough to excuse the very edit the gate exists to refuse — arriving twice in the
implementation of the paragraph that warns about it. Both are pinned:
`test_a_new_adr_naming_a_different_adr_does_not_permit_the_edit` and
`test_prose_mentioning_supersession_does_not_permit_the_edit`.

**A parsing bug in the gate's own default.** `main...HEAD` is what the tool documents and
uses when given nothing, and splitting on the literal `".."` turned it into `".HEAD"` — a
range naming nothing, reported as an empty gate rather than as the parse failure it was. A
gate whose default argument does not work is a gate nobody runs by hand. Fixed and pinned
by `test_a_three_dot_range_is_read_the_way_git_reads_it`.

**CI needs history the default checkout does not fetch, in three jobs — and the first
attempt put it on the one job that needed it least.** `actions/checkout` clones at depth 1,
which cannot diff a range at all. `fetch-depth: 0` went on `guards`, whose steps never touch
git history; meanwhile `LintUnitTests` — which drives the gate against the real `4a4221e` —
runs under `ctest` in *build-test* (twice) and *coverage*, all three at depth 1. The story
said "the guards job now sets fetch-depth: 0" and that was true and insufficient: the
assertion would have failed on three CI jobs with `unknown revision 4a4221e^..4a4221e`.
The two jobs that run `ctest` have it now.

**The range is the merge base — and the reason first recorded for it was wrong.** Three dots
resolves the diff's *old* side to the merge base, which is what the story always meant, and
it is the spelling that stays correct when the gate is run by hand against a `main` that has
moved on. The claim written here originally — that two dots would drag in everything merged
to `main` since the branch diverged — is false on a pull-request event: `github.sha` is
already the merge commit, so `base.sha` is an ancestor of it and both spellings name the same
diff. The protection comes from diffing against the merge commit, not from the separator. The
reasoning now lives once, in [quality-gates.md](../../testing/quality-gates.md); `ci.yml` and
this file point at it rather than restating it, because the same wrong sentence had been
copied into all three.

**Pull requests only, deliberately.** A push to `main` has already merged; re-judging it
would report a breach nobody can act on without a revert, and the gate would be red on
`main` for history rather than for the change under review.

**Exempt from story-0704's meta-test, correctly.** It walks no tree — it reads `git` — so it
neither calls a discovery function nor reaches `gate_files`, which is how `check_coverage`
and `check_fuzz_instrumentation` are identified as exempt. Verified by running that
meta-test's own predicates against this file rather than by assuming.


**Seven findings from the self-audit, four of them demonstrated silent passes.** Each was
reproduced by running the gate, not by reading it, and each is now pinned:

| What passed that should not have | Why |
|---|---|
| A deletion-only edit to a frozen section | `@@ -18 +17,0 @@` gains no line on the new side, so a new-side-only reading records the edit as untouched. Deleting a consequence is at least as much a breach as adding one, and quieter. Judged against the pre-image now. |
| A rename with an edit | `--diff-filter=M` never sees `R`. That filter had been added to stop a *new* ADR reporting itself as edited — it fixed that by narrowing the input without checking what else fell out. |
| Deleting an Accepted ADR outright | Same cause, `D`. Reported now as "deleted while Accepted". |
| A `**Supersedes:**` inside a code fence | An ADR documenting the ADR process would illustrate the header — and excuse whatever it named. Prose was already refused; an example is the same class arriving a third time. Fences are blanked before parsing, which also stops a fenced `## Decision` relocating the frozen span. |

And three that were the opposite error, or no answer at all:

- **An unparseable `**Status:**` was treated as "not Accepted"** — a silent pass. A
  one-character header edit would have disabled the gate for that file, permanently and
  invisibly. It is a fault now: exit 2. This is story-0704's subject, and the gate had not
  applied it to itself.
- **A supersession declared as a nested list was refused** — a false breach on exactly the
  change the gate exists to encourage, because the clause stopped at any bullet. It stops at
  a *top-level* bullet now, so `**Supersedes:**` followed by an indented list works.
- **A bare commit-ish was accepted as a range** and quietly diffed the working tree, while
  the empty-range guard passed it. It is refused with the reason.

The pattern across the four permissive ones is worth stating plainly: **every one of them
was a filter or a window narrowed to fix a previous defect, without checking what else the
narrowing excluded.** `--diff-filter=M` fixed the added-file case and lost renames and
deletions; the clause boundary fixed the character window and lost nested lists. A fix that
narrows an input set needs the same scrutiny as the defect it removes.

**A second audit round: three of those fixes were only half-closed, and two of the three
were regressions introduced by the fix itself.**

| What still reproduced | Through the path the fix did not cover |
|---|---|
| A deletion inside a frozen section | Only on a three-dot range. The pre-image was read from the left *ref*, but `a...b` measures its old side from the merge base — so the old-side line numbers were judged against a file that existed on neither side. Three dots is this gate's default and what CI passes, so a branch behind `main` is the normal case, not an edge one. |
| A rename | Fixed in the *filter*, broken in the *pathspec*. With only the new name given, git cannot pair the rename, renders the file as freshly added, and reports every line as touched — failing a slug correction and reporting Consequences nobody had touched. The existing test asserted `rc == 1`, which was true under the bug. |
| A frozen section that cannot be read | The unclosed-fence fix blanked the rest of the file, so `sections_of` found no Decision and the Decision could be rewritten freely — the same silent pass the Status fault had just removed. Renaming `## Decision` did it too. Both are faults now. |

Each is pinned by a test **watched failing with the fix reverted**, not one merely written
alongside it. The rename now asserts that *only* Decision is reported, which is the
assertion the old one was missing.

**The file outgrew the 250-line limit at 273, and the split is by subject.**
`adr_document.py` knows one record — its number, its status, its sections, what it declares
superseded — and nothing about git; `adr_range.py` knows what a range changed and nothing
about markdown; this gate is the rule that uses both. The first split was two-way and left
the policy constants and the fault type in the text module: the boundary was stated one way
and drawn another. `adr_document.py` now has unit tests of its own, because the parsing is
the hardest part of the gate and every case costs three commits when driven through a
throwaway repository — 31 of them run in 0.001s.

**No supersession excuses a deletion, deliberately.** Supersession excuses an *edit* because
the superseded record survives to be read; a successor alongside a deletion makes the loss
no smaller. An absent escape is indistinguishable from an oversight unless something says
which it is, so `test_a_superseding_record_does_not_excuse_the_deletion` says it.

**A third round, and the one that found the real hole: the gate was asking the post-image
questions that belong to the pre-image.** Every finding below was reproduced by running it
before anything was changed, and re-run afterwards.

| What passed that should not have | Why |
|---|---|
| Demote the Status and rewrite the Decision in **one** commit | "Was this Accepted?" was asked of the file *after* the change. Setting `Proposed` — or `Draft`, or `Rejected` — in the same commit that rewrote the decision exited 0. That is a general-purpose escape hatch reachable by anyone editing the file they are already editing: exactly the `--allow` flag this story refused to add, arriving as a bug. |
| `git mv` an ADR into a subdirectory | Identity came from the *new* name. Git pairs a same-content move as a rename, so no delete filter saw it, and `superseded/adr-0005-….md` does not match the ADR pattern, so no edit filter saw it either. The record left the gate in silence while the docs claimed deletion was refused. |
| A range whose diff reads nothing | The empty-range guard counted `rev-list --count a...b` — the symmetric difference — while the gate reads `merge-base(a,b)..b`. `HEAD...HEAD~3` counts three commits and diffs zero files, so a reversed or stale range expression in CI reported a clean pass over an empty diff. A vacuity hole in the vacuity milestone's own gate. |
| `diff.external` set in the reader's git config | With an external driver, `--unified=0` emits no `@@` headers at all, every hunk set comes back empty, and the gate **passes `4a4221e`** — the one commit it was written to catch. `diff.renames=false` turned a slug correction into "deleted while Accepted". A merge gate whose verdict depends on `~/.gitconfig` is not a gate; every call pins what it depends on now. |

And the two that were the opposite error:

- **The `Superseded` escape was dead code.** `breaches_in` only asks about supersession when
  a frozen section was touched, which — under the post-image reading above — already implied
  the status was still `Accepted`, so the `== "superseded"` branch could never return true.
  Its test was green through the demotion hole instead. Replacing the line with `return False`
  left the whole suite passing, which is the check that should have been run when it was
  written. Fixing the pre-image question made it load-bearing;
  `test_superseded_is_the_one_status_that_does` now fails when it is removed.
- **An unreadable pre-image had no green state.** Making an unbalanced fence a fault on both
  sides meant an ADR that *landed* malformed could never be edited, repaired or deleted —
  every route exited 2. History cannot be repaired, so the pre-image degrades; what closes it
  properly is that an Accepted ADR may no longer land malformed at all, checked when it
  arrives and can still be fixed.

The pattern, again and more precisely than the second round managed: **each fix answered the
question it was given and not the question one level up.** Renames were fixed in the filter
and left broken in the pathspec; then fixed in the pathspec and left broken in the *identity*.
Fences were fixed for examples and left broken for imbalance; then fixed for imbalance and
left with no way back. The right move each time was to ask which side of the change owns the
question — and for four of these six, the answer was "the one the code was not asking".

*(story-0703 and story-0704 flip to `Done` inside this branch's diff: both merged while this
story was in flight, and the bookkeeping landed on the next branch cut. Noted because an
unexplained status change in someone else's story is indistinguishable from an accident.)*

**A fourth round, and the two worst findings of the story were in the plumbing, not the
rule.** Four separate things made the gate exit **0 on `4a4221e`** — the single commit it
exists to catch — and none of them needed anything exotic:

| What | Why it was invisible |
|---|---|
| **Running it from any directory but the repository root** | `ADR_DIRECTORY` is a *relative* pathspec, so from `docs/` it matched no files; the range was still non-empty, so the empty-range guard was satisfied, and the gate printed "no Accepted ADR was edited in place" and exited 0. No configuration, no unusual input — just a different `cd`. Every command runs from `git rev-parse --show-toplevel` now. |
| **One committed `.gitattributes` line** marking the ADRs `-diff` | git then prints "Binary files differ" and emits no `@@` headers at all, so every hunk set came back empty. This is *in-tree*: nothing in the repository inspects it, so the change that disables the gate would arrive in the same pull request as the edit it hides. `--text`. |
| A `textconv` filter | Renumbers every line the hunk headers name, so spans are attributed to the wrong sections. `--no-textconv`. |
| An external diff driver | Replaces the patch wholesale. `--no-ext-diff`. |

**And the pins written for that in round three were aimed at the wrong layer.** `-c
diff.external=`, `-c diff.renames=true` and `-c core.quotePath=false` were each already
covered by a flag on the same command line — removing all three left the whole suite green,
which is exactly the check that should have been run when they were written. A command-line
flag outranks configuration; pinning config *as well* looked like diligence and was noise,
while the two channels that actually mattered went unpinned. Every flag now in `DIFF_FLAGS`,
and the top-level working directory, has been removed one at a time to confirm a named test
goes red:

| Removed | Test that fails |
|---|---|
| `--no-ext-diff` | `test_an_external_diff_driver_does_not_hide_the_edit` |
| `--no-textconv` | `test_a_textconv_filter_does_not_move_the_line_numbers` |
| `--text` | `test_an_attribute_marking_the_adrs_binary_does_not_hide_the_edit` |
| `-M` | `test_rename_detection_being_off_does_not_invent_a_deletion` |
| `cwd=top_level()` | `test_running_from_a_subdirectory_does_not_hide_the_edit` |

Three more, all in the rule:

- **Renumbering was a silent removal.** `git mv adr-0005-….md adr-0099-….md` touches no line
  of prose, so nothing was reported — while ADR-0005 ceased to exist under the number every
  citation to it uses. A rename is "an edit of the same record" only while it stays the same
  record.
- **The malformed check was on the wrong event.** Checking *additions* let an incomplete
  draft land legitimately and then be **promoted** to `Accepted` in a second commit, which is
  a modification; a `git mv README.md adr-0013-….md` is a rename. Both walked an incomplete
  record past the gate and wedged it exactly as before. The question belongs to arrival — is
  this `Accepted` now — not to how it arrived.
- **The pre-image fallback I added in round three reopened round three's own finding.** It
  served *no* case that exists — every one of the 83 ADR blobs in this repository's history
  parses — while allowing the escape across two commits: mangle the `**Status:**` header in
  one (it sits outside every frozen span, so it passed), demote and rewrite in the next. It
  is gone, and mangling the header is itself the fault, which is what makes the second step
  unreachable rather than merely unlikely.

The lesson is narrower than the previous rounds' and worth more: **three consecutive rounds
of fixes were correct about the rule and wrong about the plumbing underneath it.** A gate is
its input as much as its logic, and every question this one asks git — which directory,
which flags, which side of the diff — is a place the answer can be quietly emptied.

## Definition of Done

- [x] Acceptance criteria met, tests green (ASan + UBSan).
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
- [x] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
- [x] `docs/testing/quality-gates.md` updated.
