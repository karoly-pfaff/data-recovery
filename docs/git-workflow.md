<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Git & Branching Workflow

Revenant uses a **trunk-based** workflow: `main` is the trunk, and every story is
developed on a short-lived branch that merges back into `main` as a single commit. Epics
and milestones are organizational labels, **not** long-lived branches — this keeps `main`
continuously releasable and avoids integration drift.

## Branches

| Branch                 | Cut from | Merges to | Lifetime      | Purpose                                   |
|------------------------|----------|-----------|---------------|-------------------------------------------|
| `main`                 | —        | —         | permanent     | The trunk. Always releasable; all gates green. Protected: no direct commits. |
| `story/<NNNN>-<slug>`  | `main`   | `main`    | one story     | Where a single story is implemented.      |
| `fix/<slug>`           | `main`   | `main`    | short         | Hotfix for an issue outside a story.      |

Epics (`epic-m1-...`) and milestones live in the [backlog](backlog/README.md) and as
GitHub milestones/labels. They group stories; they are never branches.

## The loop

1. Pick a `Ready` story. Cut `story/0010-jpeg-carver` from the latest `main`.
2. Implement it test-first. Commit freely (WIP commits are fine on a story branch).
3. Keep current by **rebasing on `main`** as needed — the story branch is private, so
   rebasing is encouraged. Resolve conflicts here, never on `main`.
4. Open a PR: `story/0010 → main`. CI runs every [quality gate](testing/quality-gates.md).
5. Complete the story's [self-audit](code-quality.md) in the PR.
6. **Squash-merge** into `main` — one clean commit per story.

```
main  ─●──●──●──●──●──●──●──●──  one squashed commit per story, in order
        ▲     ▲     ▲     ▲
   story/0008 0009  0010  0011   short-lived, cut from main, rebased on main

  tag v0.1.0  ← placed on main when every story in milestone M1 is merged
  epic M1     = a GitHub milestone/label over those commits, not a branch
```

## One commit per story

Each merged story is **exactly one commit** on `main` (the squash). That commit:

- uses a [Conventional Commit](versioning.md) message (`feat(carve): add JPEG validating
  carver`);
- references its story in the footer (`Story: story-0010`);
- builds and passes every gate on its own, so `main` is releasable at every commit.

## Releases & milestones

A milestone completes when all its stories are merged and `main` is green. At that point
we tag the release (`v0.1.0`) and finalize `CHANGELOG.md` per
[versioning](versioning.md). No release branch is needed; we tag the trunk. If a released
version needs a patch later, branch `fix/<slug>` from the tag, fix, and tag `v0.1.1`.

## Rules

- **Never commit directly to `main`.** Everything arrives via a `story/` (or `fix/`) PR.
- **Every PR is green** on all quality gates before merge — no exceptions.
- **Every commit references its story** (or is an explicit hotfix).
- **No merge without review** — CI plus the story self-audit.
- **Keep stories small.** A story that will not land in a focused branch is too big;
  split it. Small stories are what makes trunk-based flow work.
- Rebase your own `story/` branch freely; never rewrite history on `main`.

## Pre-commit hook

A versioned pre-commit hook lives in [`.githooks/pre-commit`](../.githooks/pre-commit).
Enable it once per clone:

```bash
git config core.hooksPath .githooks
```

It enforces, at the commit boundary, regardless of how a file was edited:

1. **`AGENTS.md` and `CLAUDE.md` are frozen** — commits touching them are rejected. This
   closes the gap that permission `deny` rules cannot cover (e.g. a shell write), because
   the check runs on the staged diff, not on the editing tool. The maintainer may override
   intentionally with `git commit --no-verify`.
2. The **250-line file-length guard**.
3. **`clang-format`** on staged C++ (skipped gracefully if the tool is absent).

Together with the `.claude/settings.json` deny rules (which stop the assistant's
`Edit`/`Write` up front), this gives defense in depth for the frozen contract.
