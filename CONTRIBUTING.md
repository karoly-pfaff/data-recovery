<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Contributing

Thank you for helping build Revenant. This project holds itself to a high, mechanically
enforced standard. Contributions that meet it merge smoothly; the tooling exists to make
meeting it easy and automatic.

If you are working here as an AI agent, read [`CLAUDE.md`](CLAUDE.md) as well — it covers
the tooling that is specific to that.

## Before you start

1. Read [`AGENTS.md`](AGENTS.md) — the binding engineering contract. It is short.
2. Read [`docs/code-quality.md`](docs/code-quality.md) for the reasoning behind it, and
   [`docs/testing/strategy.md`](docs/testing/strategy.md) for what "tested" means here.
3. Pick or write a story in [`docs/backlog/`](docs/backlog/README.md). No code without a
   story — that is how YAGNI is enforced in practice.

## Setting up

[`docs/install.md`](docs/install.md) has the prerequisites, the pinned tool versions, the
per-platform commands and the known caveats. Follow it once; everything after that is
`cmake --preset debug`.

## The loop

1. Cut a branch from `main` and work on it —
   [`docs/git-workflow.md`](docs/git-workflow.md) owns branch naming, the pull request,
   and how it lands.
2. **Test first.** Write the failing test, then the code
   ([`docs/testing/strategy.md`](docs/testing/strategy.md)).
3. Run the gates locally before you push. What they are and how to reproduce each one is
   in [`docs/testing/quality-gates.md`](docs/testing/quality-gates.md).
4. Update `CHANGELOG.md` under `[Unreleased]` if the change is user-facing
   ([`docs/versioning.md`](docs/versioning.md)).
5. Complete the story-level self-audit checklist in
   [`docs/code-quality.md`](docs/code-quality.md). A "no" is rework, not a note to self.
6. Open a pull request referencing the story. CI must be green; review confirms the
   self-audit and the design.

## Commits and pull requests

- [Conventional Commits](docs/versioning.md); small, buildable commits.
- A pull request says what changed and why, links its story, and notes any ADR added or
  affected.
- **If your change alters a documented design decision, add or update an ADR in the same
  pull request.** See [`docs/architecture/adr/`](docs/architecture/adr/README.md).

## Adding a carve format

Do not hand-roll it — invoke the `add-format-carver` skill. It scaffolds the carver, its
registration, and the mandatory unit and fuzz tests. See [`AGENTS.md`](AGENTS.md) §7.

## Reporting security issues

Parsing untrusted bytes is the core of this tool, so a crash on a crafted input is a
security issue, not just a bug. [`SECURITY.md`](SECURITY.md) is the policy: what counts,
how to report it, and what to include.

## License

By contributing you agree your contributions are licensed under GPL-3.0-or-later.
