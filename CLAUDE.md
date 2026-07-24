<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# CLAUDE.md — Working in this repository

## Read this first (mandatory)

Before writing or changing any code, read **[`AGENTS.md`](AGENTS.md)**. It is the
binding engineering contract: naming, hard limits, clean-code rules, testing, and
quality gates. Everything below assumes you have internalized it.

The Prime Directive governs everything:

> **A function does exactly one thing, at exactly one level of abstraction.**

## What this project is

**Revenant** — a data-recovery toolkit aiming to be a more precise successor to
PhotoRec/TestDisk. Two frontends over a shared static core (`librevenant`):

- **`revenant-carve`** — structure-aware, *validating* file carving. Every supported
  format is parsed to determine its exact length; we never "grab bytes until the next
  header". This is the project's core differentiator.
- **`revenant-undelete`** — filesystem-aware recovery (NTFS, FAT32, exFAT, ext4) that
  restores original names, paths, and timestamps. Runs the carve engine over
  unallocated space in hybrid mode.

Read-only by default: the source device is never modified.

## Where things live

- `include/revenant/`, `src/{core,volume,fs,carve/formats,recovery,cli}/` — code.
- `tests/{unit,integration,fixtures,fuzz}/` — tests and synthetic disk images.
- `tools/` — disk-image generators and test-corpus builders.
- `docs/` — architecture, roadmap, backlog, testing, quality, versioning, performance.
- `.claude-plugin/` + `skills/` — the `revenant:*` project skills.

## Build & test commands

Configuration is driven by `CMakePresets.json`. Dependencies come from vcpkg
(`vcpkg.json` manifest).

```bash
# Configure (Debug with sanitizers + tests)
cmake --preset debug

# Build
cmake --build --preset debug

# Run the full test suite
ctest --preset debug --output-on-failure

# Release build
cmake --preset release && cmake --build --preset release
```

Lint/format locally before committing (also runs in CI and as a pre-commit hook):

```bash
cmake --build --preset debug --target format-check   # clang-format
cmake --build --preset debug --target tidy            # clang-tidy
cmake --build --preset debug --target guard-limits    # file-length / size guards
```

## Workflow expectations

- **TDD by default.** Write the failing test first (`tests/unit/…`), then the code.
- **One story at a time.** Work references a `docs/backlog/stories/story-*.md`.
- **Adding a carve format?** Do not hand-roll it — invoke `revenant:add-format-carver`.
- **Every change** updates `CHANGELOG.md` under `[Unreleased]` and completes the
  story-level self-audit checklist in `docs/code-quality.md`.
- Commit messages follow **Conventional Commits**.

## Non-negotiables (full list in `AGENTS.md`)

- Never write to the source device outside an explicit, guarded write path.
- No file over 250 lines, no function over 10 statements / complexity 10.
- No production code without a test; every byte-parser has a fuzz target.
- `-Wall -Wextra -Werror`, clang-tidy clean, ASan+UBSan green — all merge gates.
