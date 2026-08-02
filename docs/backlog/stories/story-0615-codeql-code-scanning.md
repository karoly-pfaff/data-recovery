<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0615: CodeQL reads the parsers the way an attacker would

- Epic: [epic-m6-loose-ends](../epic-m6-loose-ends.md)
- Status: Ready
- Size: S

## Goal

Put GitHub's CodeQL analysis on this repository, non-blocking to begin with, so that
untrusted bytes are traced from the device read to the arithmetic they end up in —
across functions and files, which is the one question none of the existing gates ask.

## Design references

- [ADR-0003](../../architecture/adr/adr-0003-validating-carving.md) — precision over
  recall. Every parser in `fs/`, `volume/` and `carve/` reads bytes an attacker or a
  failing disk chose; that is the threat model this analysis is aimed at.
- [quality-gates.md](../../testing/quality-gates.md) — where a new gate is declared, and
  the table saying which job runs which check on which platform.
- [epic-m6](../epic-m6-loose-ends.md#notes) — the note this story replaces: "lands here
  or nowhere before 1.0", with the reasoning for why M5 and M7 are both worse.
- [story-0612](story-0612-ci-runs-gate-targets.md) — a gate that only CI can run is the
  shape that milestone found wrong. This one genuinely cannot run locally, so it is
  declared as CI-only rather than left to look like an oversight.

## What was measured

- **CodeQL is free for this repository**, which it was not before the repository went
  public. No plan, no per-seat cost.
- **The C++ analysis needs a real build.** CodeQL's extractor observes the compiler, so
  the job compiles the tree from scratch. Whatever it costs, it costs on top of the
  existing jobs in billed minutes; in wall clock it runs concurrently with them.
- **CI stands at roughly 13 minutes** and the project has already decided that under 15
  is acceptable ([ci-speed decisions in M5](epic-m5-performance.md)). A parallel job
  changes wall clock only if it is the longest one.
- **The existing gates ask a narrower question.** clang-tidy works a translation unit at
  a time; the fuzzers find what their inputs reach. Neither traces a value from
  `BlockDevice::readAt` through three functions into an allocation size. The overflow
  guards this milestone added — `safeMul64`, `safeAdd64`, `saturatingAdd64` — were each
  put there by a person noticing, not by a machine.

## Design decisions

**Non-blocking first, and gating only on evidence.** The job runs and reports; a finding
does not fail the build. Two reasons. A first CodeQL run over an unanalysed C++ tree
produces a backlog of alerts whose signal-to-noise nobody here has measured yet, and
turning that into red merges on day one is how a gate gets switched off rather than
fixed. And [story-0612](story-0612-ci-runs-gate-targets.md)'s lesson cuts the other way
too: a gate is worth having when someone acts on it, not when it merely exists.
Promoting it to blocking is a follow-up with a number behind it.

**Scheduled plus pull requests to `main`, not every push.** Every push would pay a full
build for a question whose answer changes slowly. A weekly schedule catches drift; a PR
run catches the change that introduced it, which is when it is cheapest to fix.

**The default query suite, unmodified.** `security-and-quality` is what GitHub maintains
and what the alerts are written against. Custom QL is a thing to want *after* the default
suite has been read and found wanting, not before.

**It is declared CI-only, in the table.** CodeQL cannot run on a developer's machine
without the CLI and a database build, and pretending otherwise is the failure story-0612
was written about. `quality-gates.md` gains a row saying so, with the reason, so the next
person reading that table does not go looking for a local target that was never there.

**What the first run's findings become.** Each alert is either a fix, or a dismissal with
a written reason in the Security tab — never left open and unread. If the first run turns
up something real, it becomes its own story rather than being folded in here: this story
is the analysis arriving, not a promise about what it will say.

## Acceptance criteria

- [ ] A CodeQL workflow analyses the C++ tree on pull requests targeting `main` and on a
      weekly schedule, and does not fail a build on findings.
- [ ] The run appears in the repository's Security tab with the `security-and-quality`
      suite, over a build that actually compiled the tree — an empty or partial database
      is a failure, not a pass.
- [ ] Every alert from the first run is dismissed with a stated reason or has a story.
- [ ] [quality-gates.md](../../testing/quality-gates.md) records the check, that it is
      CI-only and why, and that it is non-blocking pending a decision to gate.
- [ ] `CHANGELOG.md` is untouched: this changes no behaviour an operator can see.
- [ ] The epic's CodeQL note is replaced by a link to this story.

## Test plan

There is no unit test for a CI workflow, and inventing one would be the vacuous kind of
check this project has already been bitten by. What stands in for it:

- **The run is watched failing on purpose once.** A branch that introduces an obvious
  taint-flow finding — an unvalidated on-disk length used as an allocation size in a
  scratch file — must produce an alert. Without that, a green CodeQL job proves only that
  the job ran. Delete the branch afterwards; record what the alert said in this story.
- **The database is confirmed non-empty**: the job log states the number of files
  extracted, and it is compared against the tree's translation-unit count.
- The existing suite must be unaffected: no new job may change the outcome of the others.

## Definition of Done

- [ ] Acceptance criteria met.
- [ ] The deliberate-finding run is recorded in this story, with what CodeQL said.
- [ ] Epic row linked and the epic's note replaced.
- [ ] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
