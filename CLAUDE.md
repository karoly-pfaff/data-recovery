<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# CLAUDE.md — Working here as an agent

## Read these first (mandatory)

1. **[`AGENTS.md`](AGENTS.md)** — the binding engineering contract. Naming, hard limits,
   clean-code rules, testing, the non-negotiables. Everything below assumes it.
2. **[`CONTRIBUTING.md`](CONTRIBUTING.md)** — the contribution loop, which applies to you
   unchanged. [`README.md`](README.md) is the map to everything else.

This file adds only what is different about working here *as an agent*. It deliberately
restates none of the rules: if a rule appears here and in `AGENTS.md`, that is a bug in
this file.

One rule is worth repeating anyway, because it is the one that cannot be undone:
**never write to the source device.** Not by policy — by construction, and there is a
test that fails if it stops being true
([ADR-0005](docs/architecture/adr/adr-0005-read-only-by-default.md)).

## The tooling in `.claude/`

- **Skills** — `start-story`, `finish-story`, `add-format-carver`, `milestone-audit`,
  `fuzz-campaign`, `wsl-bench`.
- **Subagents** — `gate-runner` (runs the local gates out of the main context and reports
  compactly), `story-auditor` (adversarial, read-only self-audit).
- **Hooks** — auto-format on edit, tidy-stamp invalidation on header edits, and a
  commit-attribution guard. Self-tested: `python .claude/hooks/test_hooks.py`.
- **`settings.json`** — permissions and hook wiring. `AGENTS.md` and `CLAUDE.md` are
  denied to `Edit`/`Write` and frozen by the pre-commit hook; changing either is a
  maintainer action.

New subagents need a session restart; hooks and skills hot-reload.

## The story lifecycle is skill-driven

- Picking up work → **`start-story`**. One story at a time.
- Taking it to Done → **`finish-story`**, which delegates the local gates and the MSVC
  blind-spot sweep to `gate-runner`, and the self-audit to `story-auditor`.
- At a milestone boundary → **`milestone-audit`** before the next milestone's stories are
  finalized.
- Adding a carve format → **`add-format-carver`**. Do not hand-roll it.
- Long fuzz runs → **`fuzz-campaign`**. Anything needing Linux locally — loop devices,
  libFuzzer, valgrind — → **`wsl-bench`**.

## What the hooks do at the boundary

Editing a C++ file runs clang-format over it, and editing a header clears the tidy stamps
so the next `tidy` run cannot come back falsely green. At `git commit`, a hook rejects any
AI-attribution footer; `.githooks/pre-commit` additionally refuses changes to the frozen
files and re-checks formatting and file length. See
[`docs/git-workflow.md`](docs/git-workflow.md) for the git-side hook.

## Where things live

Code and test layout: [architecture overview](docs/architecture/overview.md) has the
module map. Build and test commands: [`docs/install.md`](docs/install.md). Which gate
fires when: [`docs/testing/quality-gates.md`](docs/testing/quality-gates.md).
