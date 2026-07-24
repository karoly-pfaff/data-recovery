<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Contributing

Thank you for helping build Revenant. This project holds itself to a high, mechanically
enforced standard. Contributions that meet it merge smoothly; the tooling exists to make
"meeting it" easy and automatic.

## Before you start

1. Read [`AGENTS.md`](../AGENTS.md) — the binding engineering contract.
2. Read [`code-quality.md`](code-quality.md) and [`testing/strategy.md`](testing/strategy.md).
3. Pick or write a story in [`backlog/`](backlog/README.md). No code without a story.

## Environment

- A C++20 compiler: MSVC 2022, GCC 13+, or Clang 16+.
- CMake ≥ 3.25, Ninja, and a vcpkg checkout (`VCPKG_ROOT` set).
- `clang-format` and `clang-tidy` (matching versions across contributors).
- Python 3 for the lint/guard scripts.

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

## Workflow

1. Cut a `story/<NNNN>-<slug>` branch from `main` (trunk-based; one branch per story,
   squash-merged back to `main` as one commit). See [git-workflow.md](git-workflow.md).
2. **Test first** (TDD): write the failing test, then the code.
3. Run the local pre-flight (format-check, tidy, guard-limits, tests) — or enable the
   versioned pre-commit hook so it runs automatically (once per clone):
   `git config core.hooksPath .githooks` (see [git-workflow.md](git-workflow.md)).
4. Update `CHANGELOG.md` under `[Unreleased]`.
5. Complete the story-level self-audit checklist in `code-quality.md`.
6. Open a PR referencing the story. CI must be green; a human/AI review confirms the
   self-audit and design.

## Commits & PRs

- [Conventional Commits](versioning.md); small, buildable commits.
- A PR describes what changed and why, links its story, and notes any ADR added or
  affected.
- If your change alters a documented design decision, add or update an ADR in the same
  PR.

## Adding a carve format

Do not hand-roll it. Invoke the guided skill:

```
revenant:add-format-carver
```

It scaffolds the carver, its registration, and the mandatory unit + fuzz tests, and
reminds you of the docs/changelog updates.

## Reporting security issues

Parsing untrusted bytes is the core of this tool. If you find an input that causes a
crash, hang, or out-of-bounds access, treat it as security-relevant: include the input
as a regression corpus entry and, for undisclosed issues, contact the maintainers
privately before public disclosure.

## License

By contributing you agree your contributions are licensed under GPL-3.0-or-later.
