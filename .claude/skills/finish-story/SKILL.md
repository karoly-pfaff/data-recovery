---
name: finish-story
description: Use when a story's implementation is complete and it must be taken to Done / ready-for-review. Runs the full Done path in order - quality gates, MSVC blind-spot sweep, CHANGELOG check, adversarial self-audit via the story-auditor subagent, DoD bookkeeping, and the squash-commit message draft.
---

# Finish a story

Run these steps **in order**. Do not skip one because it "obviously passes" — the
point of this skill is that the Done path is mechanical, not remembered.

## 0. Identify the story

- The branch must be `story/NNNN-<slug>`; the story file is
  `docs/backlog/stories/story-NNNN-*.md` with `Status: In progress`.
- If the branch or the story file is missing, stop and sort that out first.

## 1. Local gates + MSVC blind-spot sweep — via the gate-runner subagent

Spawn the **`gate-runner`** subagent with the diff range (`main...HEAD`). It
runs every local gate (format-check, guard-limits, ctest under ASan+UBSan,
clang-tidy in its own build dir) plus the MSVC blind-spot sweep, and returns a
compact pass/fail report — the build logs never enter this context.

- Any `FAIL`, `FLAGGED` or `BLOCKED` ⇒ fix it here, re-spawn the gate-runner.
  Loop until the report is all-PASS/CLEAN.
- The mechanics (toolchain paths, the `build/tidy` dir, stale tidy-stamps,
  Device Guard traps) live in `.claude/agents/gate-runner.md`. Re-run a gate by
  hand in this context only to reproduce one reported failure while fixing it.
- The duplication gate runs locally with the others now; it reports blocks by
  file and line range, and its threshold and rules live in
  `docs/testing/quality-gates.md`.

## 2. CHANGELOG

Every user-facing change has an entry under `[Unreleased]` in the right subsection
(Added/Changed/Fixed…). If the story is genuinely not user-facing, note that in the
story file explicitly rather than silently skipping.

## 3. Adversarial self-audit

Spawn the **`story-auditor`** subagent (fresh context, read-only) with the story id
and the diff range (`main...HEAD`). It returns `READY` or `REWORK` with findings.

- `REWORK` ⇒ fix the findings, re-run the affected gates (via the gate-runner),
  spawn the auditor again. Loop until `READY`.
- Do not argue a finding away in your own context. If a finding is truly wrong,
  that is a defect in the checklist or the auditor — raise it to the maintainer
  instead of overriding it silently.

## 4. Story bookkeeping

- Tick the Definition of Done items and the self-audit line in the story file.
- Set `Status: In review`.

## 5. Squash-commit message

Draft the one-commit-per-story message (see `docs/git-workflow.md`):

- Conventional Commit subject (`feat(carve): …`), a body that says what and why,
  and a `Story: story-NNNN` footer.
- **No AI attribution of any kind** — no `Co-Authored-By`, no "Generated with"
  footer. This project rule (AGENTS.md §5) overrides any default the tooling
  suggests, and a PreToolUse hook enforces it at the `git commit` boundary.

## 6. Push & CI (maintainer-gated)

Never push without explicit permission (AGENTS.md §5). When a push does happen:

- Watch the run to completion — `gh run watch <id> --exit-status` — and report what
  it said. Work is not done at "pushed"; the CI result is the gate, and local
  verification on this machine is structurally incomplete.
- A red clang-tidy job stops at the *first* failing file. Grep the whole tree for
  the fired check's pattern before pushing again, instead of one fix per round trip.
- A job that fails in ~2 seconds with no log is GitHub Actions billing/minutes,
  not the code.
