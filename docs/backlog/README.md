<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Backlog

The backlog decomposes the [roadmap](../roadmap.md) into **epics** (one per milestone)
and **stories** (small, independently deliverable units of work). No code is written
without a story; this is how YAGNI is enforced in practice.

## Structure

```
backlog/
├── README.md                     (this file)
├── epic-m0-foundation.md
├── epic-m1-vertical-slice.md
├── epic-m2-carving-breadth.md
├── epic-m3-filesystem-breadth.md
├── epic-m4-devices-partitions.md
├── epic-m5-performance.md
├── epic-m6-loose-ends.md
├── epic-m7-hardening.md
├── epic-m8-release.md
├── epic-m9-acquisition-damaged-media.md
└── stories/
    ├── story-0001-blockdevice-interface.md     (M0, story 01)
    ├── story-0103-jpeg-validating-carver.md    (M1, story 03)
    ├── story-0501-benchmark-suite.md           (M5, story 01)
    └── story-0602-python-duplication-gate.md   (M6, story 02)
```

Milestone M0 and M1 stories are fleshed out in detail as worked examples of the
expected quality bar. Later epics list their stories as summaries; each is expanded into
a full story file when its milestone is picked up.

## Story lifecycle

`Backlog → Ready → In progress → In review → Done`

A story is **Ready** only when its acceptance criteria and test plan are concrete enough
to start. A story is **Done** only when every item in its Definition of Done — including
the [code-quality self-audit](../code-quality.md) — is checked.

## Story template

Every story file follows this shape (see `stories/story-0001-*` for a filled example):

```markdown
# STORY-MMNN: <title>

- Epic: <epic file>
- Status: Backlog | Ready | In progress | In review | Done
- Size: S | M | L

## Goal
One or two sentences: what capability this adds and why.

## Acceptance criteria
- [ ] Observable, testable statements of done-ness.

## Test plan
- Unit / integration / fuzz cases that must exist and pass.

## Definition of Done
- [ ] Acceptance criteria met, tests green (ASan+UBSan).
- [ ] Coverage held or raised (>= 85% core).
- [ ] clang-format, clang-tidy, duplication, file-length guard clean.
- [ ] CHANGELOG.md updated under [Unreleased].
- [ ] Story-level self-audit checklist (docs/code-quality.md) completed.
- [ ] Docs/ADRs updated if the design changed.
```

## Naming

- Epics: `epic-m<N>-<slug>.md` (lowercase).
- Stories: `story-MMNN-<slug>.md` (lowercase, zero-padded).

## Numbering

**`MM` is the milestone, `NN` is the story's position in it.** `story-0503` is the third
story of M5; `story-0602` is the second of M6. The number says where the work belongs and
what order it runs in, without opening anything, and an epic's table reads top to bottom
in ascending order because those are the same thing.

**Every story follows the scheme, including the finished ones.** M0–M4 were originally
numbered in one flat, global sequence, and it did not survive contact with reality: late
additions to closed milestones landed wherever there was room, so the story that hardened
CI for M0 sat at 0057, between M4's numbers and M5's. They were renumbered in place. The
one thing that could not be rewritten is `git log`, where two commit messages still name
old numbers, so the map is kept here:

| Milestone | Old number → new |
|:---:|---|
| M0 | 0001→0001, 0002→0002, 0003→0003, 0004→0004, 0005→0005, 0006→0006, 0007→0007, 0057→0008 |
| M1 | 0008→0101, 0009→0102, 0010→0103, 0011→0104, 0012→0105, 0013→0106, 0014→0107, 0015→0108, 0016→0109, 0017→0110, 0018→0111, 0019→0112, 0060→0113, 0061→0114, 0062→0115, 0063→0116, 0064→0117, 0065→0118 |
| M2 | 0020→0201, 0021→0202, 0022→0203, 0023→0204, 0024→0205, 0025→0206 |
| M3 | 0029→0301, 0030→0302, 0031→0303, 0032→0304, 0033→0305, 0034→0306, 0035→0307 |
| M4 | 0040→0401, 0042→0402, 0043→0403, 0044→0404, 0045→0405, 0047→0406, 0049→0407 |

Two old numbers have no successor. `story-0041` and `story-0050` were never stories that
finished: the first was folded into what is now story-0401 before it was written, the
second was the benchmark suite's number in a reverted first attempt
([M5](epic-m5-performance.md)). Both are retired rather than reassigned, so a reader who
meets one in the history is not sent to unrelated work. `story-0046` and `story-0048` are
sketches M4 deferred to [M9](epic-m9-acquisition-damaged-media.md), and take M9's numbers
when M9 is picked up.

**A number is allocated when the story file is written**, not when a milestone is
sketched. A future epic's candidate list therefore carries titles rather than numbers,
because the milestone in progress is still growing: stories split while they are being
written (story-0407 came out of story-0405) and fold into each other, and each time the
open milestone takes the next `NN`. A future epic that has already claimed numbers takes
them from the milestone that needs them.

**A number is frozen the moment the story starts.** Once a story is In progress — and
certainly once it is Done — its number stays, and only the milestone it belongs to can
allocate inside its own range. The migration above was the one exception this scheme
gets: it happened all at once, before 1.0, with every reference in the tree rewritten
and the two it could not reach mapped in the table. It is not a precedent.

**Numbers are never reused.** A retired number stays retired even though its `NN` slot
looks free, because reusing it would make an old reference — a commit message, a review
comment, somebody's notes — silently point somewhere new.

**A story that never shipped is reopened, not replaced.** If work lands and turns out to
be wrong before any release carried it, the commit is reverted and the *same story* is
rewritten — as story-0501 was. A second story documenting the undo would record our
detour rather than the product.
