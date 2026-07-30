<!--
  SPDX-License-Identifier: GPL-3.0-or-later
  This file is the binding engineering contract for the Revenant project.
  It is MANDATORY reading for every contributor, human or AI, before any change.
  Rationale and detail live in docs/; this file is the short, enforceable summary.
-->

# AGENTS.md — Engineering Contract

**This is a contract, not a suggestion.** Every rule here is enforced by CI. A change
that violates any rule does not merge. If a rule is wrong, change the rule in a
dedicated PR with justification — do not bypass it.

Detailed rationale lives under [`docs/`](docs/). This document is the binding
summary and the single source of truth for *what must be true* of the code.

---

## 0. The Prime Directive

> **A function does exactly one thing, at exactly one level of abstraction.**

If you cannot describe a function without the word "and", it does too much. Split it.
Statement counts and complexity limits below are the *mechanical floor* of this rule —
passing them is necessary but not sufficient. The real test is single responsibility.

---

## 1. Naming (enforced by clang-tidy `readability-identifier-naming`)

| Element                              | Style                       | Example                        |
|--------------------------------------|-----------------------------|--------------------------------|
| Types (class/struct/enum/concept)    | `PascalCase`                | `BlockDevice`, `JpegCarver`    |
| Functions / methods                  | `camelCase`                 | `readSector`, `findNextHeader` |
| Variables, parameters                | `camelCase`                 | `sectorSize`, `mftEntry`       |
| Private data members                 | `camelCase` + trailing `_`  | `deviceHandle_`, `cache_`      |
| Constants / enumerators              | `PascalCase` with `k` prefix| `kSectorSize`, `kMaxScanDepth` |
| Namespaces                           | lowercase, short            | `revenant`, `revenant::carve`  |
| Macros (use sparingly)               | `UPPER_SNAKE`               | `REVENANT_ASSERT`              |
| File names                           | `PascalCase`, per main type | `BlockDevice.hpp` / `.cpp`     |

## 2. Hard limits (enforced by clang-tidy + CI scripts)

| Rule                        | Warn | **Hard fail (CI red)** |
|-----------------------------|:----:|:----------------------:|
| File length (lines)         | 200  | **250**                |
| Statements per function     | 8    | **10**                 |
| Cyclomatic complexity       | 8    | **10**                 |
| Nesting depth               | 3    | **4**                  |
| Parameter count             | 4    | **5**                  |
| Function length (lines)     | 40   | **60**                 |

A file approaching 250 lines is a signal it holds more than one responsibility.
Split by responsibility, not by line count.

## 3. Clean-code rules (reviewed every story; see `docs/code-quality.md`)

- **One function, one thing, one abstraction level.** (Prime Directive.)
- **DRY** — no copy-paste logic. The duplication detector fails CI on clones.
- **SOLID** where applicable — especially SRP (types) and DIP (depend on interfaces
  like `BlockDevice`, `FormatCarver`, not concretes).
- **YAGNI** — no speculative abstraction. No code without a backing story. No "might
  need it later" parameters, hooks, or generality.
- **No magic numbers** — name every constant (`kSectorSize`, not `512`).
- **Fail loud, fail typed** — errors are values (`Result<T>`), never swallowed. No
  empty `catch`, no ignored return codes, no silent fallbacks.
- **Read-only by default** — the source device is NEVER written. Output goes to a
  separate destination. Any write path is explicit, guarded, and opt-in.
- **No undefined behavior in byte code** — use `std::span`, `std::bit_cast`, and the
  endianness helpers. No `reinterpret_cast`-and-pray, no unaligned deref, no signed
  overflow. Sanitizers (ASan/UBSan) are a merge gate.

## 4. Testing (see `docs/testing/strategy.md`)

- **No production code without a test.** TDD is the default: red → green → refactor.
- Every parser has **unit tests** with hand-crafted byte fixtures, including malformed
  and truncated inputs.
- Every parser that reads external bytes has a **fuzz target** (libFuzzer). Parsing
  hostile/corrupt data is the core threat model — fuzzing is a gate, not a nicety.
- Coverage floor on core logic: **≥ 85%** (CI-enforced).

## 5. Commits & versioning (see `docs/versioning.md`)

- **Conventional Commits** (`feat:`, `fix:`, `refactor:`, `test:`, `docs:`, …).
- **SemVer** for releases. **Keep a Changelog** format in `CHANGELOG.md`.
- Every user-facing change updates `CHANGELOG.md` under `[Unreleased]`.
- **No watermark in commits.** Commit messages carry no tool/assistant attribution — no
  `Co-Authored-By` for AI tools, no "Generated with" footers, no watermark of any kind.
  A `.claude` PreToolUse hook rejects tainted messages at the commit boundary.
- **Push policy:** the repository is private during development. Never push without
  explicit permission; pushes happen only at minor releases, after a full audit. See
  `settings.md`.

## 6. Every change must

1. Reference a story (`docs/backlog/stories/story-*.md`) or be an explicit hotfix.
2. Pass `clang-format`, `clang-tidy`, cppcheck, the duplication detector, and the
   file-length guard — all clean, no suppressions without an inline justification.
3. Build clean with `-Wall -Wextra -Werror` on both Windows (MSVC) and Linux (GCC/Clang).
4. Pass all tests under ASan + UBSan.
5. Keep or raise coverage.
6. Complete the **story-level self-audit checklist** in `docs/code-quality.md`.

## 7. Adding a new carve format

Do not hand-roll it. Use the guided skill:

```
add-format-carver
```

It enforces file placement (`src/carve/formats/`), naming, the mandatory
unit + fuzz tests, registry wiring, and `CHANGELOG` / docs updates.

---

*When in doubt, prefer the smaller, clearer, better-tested option. Clarity is the
feature. See [`CLAUDE.md`](CLAUDE.md) for build/test commands and agent workflow.*
