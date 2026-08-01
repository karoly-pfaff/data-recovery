<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# CLAUDE.md — Working here as an agent

## Read these first (mandatory)

1. **[`AGENTS.md`](AGENTS.md)** — the binding engineering contract. Naming, hard limits,
   clean-code rules, testing, the non-negotiables. Everything below assumes it.
2. **[`CONTRIBUTING.md`](CONTRIBUTING.md)** — the contribution loop, which applies to you
   unchanged. [`README.md`](README.md) is the map to everything else.

This file adds only what is different about working here *as an agent*. It restates the
rules exactly once, immediately below, and nowhere else: any *other* rule that appears
both here and in `AGENTS.md` is a bug in this file.

That one exception is the rule that cannot be undone:
**never write to the source device.** Not by policy — by construction.
[ADR-0011](docs/architecture/adr/adr-0011-two-halves-of-the-read-only-guarantee.md)
separates the half that is structural from the half that is only a check, and says which
one a given claim is standing on.

## The tooling in `.claude/`

- **Skills** — `start-story`, `finish-story`, `add-format-carver`, `milestone-audit`,
  `fuzz-campaign`, `wsl-bench`.
- **Subagents** — `gate-runner` (runs the local gates *and the Linux leg* out of the main
  context and reports compactly), `story-auditor` (adversarial, read-only self-audit).
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
- Adding a carve format → **`add-format-carver`** ([`AGENTS.md`](AGENTS.md) §7).
- Long fuzz runs → **`fuzz-campaign`**. Anything needing Linux locally — loop devices,
  libFuzzer, valgrind — → **`wsl-bench`**.

## Where things live

Source layout: [architecture overview](docs/architecture/overview.md) has the module map.
Tests live under `tests/{unit,integration,fixtures,fuzz}/` and the generators under
`tools/`; [`docs/testing/strategy.md`](docs/testing/strategy.md) explains what belongs in
each. Build and test commands: [`docs/install.md`](docs/install.md). Which gate fires
when: [`docs/testing/quality-gates.md`](docs/testing/quality-gates.md).
