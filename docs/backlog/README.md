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
├── epic-m5-hardening-release.md
└── stories/
    ├── story-0001-blockdevice-interface.md
    ├── story-0002-image-file-device.md
    ├── story-0003-result-and-byte-utilities.md
    └── story-0010-jpeg-validating-carver.md
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
# STORY-NNNN: <title>

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
- Stories: `story-NNNN-<slug>.md` (lowercase, zero-padded, globally sequential).
