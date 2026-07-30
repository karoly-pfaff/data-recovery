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
- **Hooks** — three, wired in `settings.json` and self-tested by
  `python .claude/hooks/test_hooks.py`. Editing a C++ file runs clang-format over it;
  editing a header clears the tidy stamps, so the next `tidy` run cannot come back
  falsely green; and a `git commit` carrying an AI-attribution footer is rejected.
- **`settings.json`** — permissions and hook wiring. It denies `Edit`/`Write` on
  `AGENTS.md` and `CLAUDE.md`, which `.githooks/pre-commit` also refuses to commit; see
  [`docs/git-workflow.md`](docs/git-workflow.md) for that half and its override.

New subagents need a session restart; hooks and skills hot-reload.

## The story lifecycle is skill-driven

- Picking up work → **`start-story`**. One story at a time.
- Taking it to Done → **`finish-story`**, which delegates the local gates and the MSVC
  blind-spot sweep to `gate-runner`, and the self-audit to `story-auditor`.
- At a milestone boundary → **`milestone-audit`** before the next milestone's stories are
  finalized.
- Adding a carve format → **`add-format-carver`**, as [`AGENTS.md`](AGENTS.md) §7
  requires.
- Long fuzz runs → **`fuzz-campaign`**. Anything needing Linux locally — loop devices,
  libFuzzer, valgrind — → **`wsl-bench`**.

## Where things live

Code and test layout: [architecture overview](docs/architecture/overview.md) has the
module map. Build and test commands: [`docs/install.md`](docs/install.md). Which gate
fires when: [`docs/testing/quality-gates.md`](docs/testing/quality-gates.md).
