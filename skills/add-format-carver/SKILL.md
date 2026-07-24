---
name: add-format-carver
description: Use when adding support for a new file format to the Revenant carving engine. Guides creating a validating FormatCarver (exact-extent parser), registering it, and its mandatory unit + fuzz tests, all within the revenant::carve namespace and the project quality gates.
---

# Adding a Revenant carve format

Use this skill to add a new file format to the `revenant-carve` engine. Revenant does
**validating carving**: a carver parses a format's internal structure to compute the
file's *exact* extent and a confidence verdict — it never "collects bytes until a
footer or size cap". Read
[`docs/architecture/carving-engine.md`](../../docs/architecture/carving-engine.md) and
[`ADR-0003`](../../docs/architecture/adr/adr-0003-validating-carving.md) before starting.

Everything you write lives in the `revenant::carve` namespace and must satisfy
[`AGENTS.md`](../../AGENTS.md): files ≤ 250 lines, functions ≤ 10 statements doing one
thing, `PascalCase` types / `camelCase` functions, and full test coverage including a
fuzz target.

## Before you start

- There must be a **story** for this format (see `docs/backlog/`). No code without one.
- Confirm the format is not already registered in `CarverRegistry`.
- Gather the format's structure: header signature(s), how length is determined, and the
  structural invariants that make a candidate *valid* vs *uncertain* vs *rejected*.

## Checklist

Create one todo per item and complete them in order.

1. **Signature & extent research.** Write down, in the story or a scratch note: the
   magic byte pattern(s) and offset; the exact algorithm to find the end of the file by
   walking its structure; and the invariants distinguishing `Valid` / `Uncertain` /
   `Rejected`. If you cannot describe how to compute the exact length, stop — a
   signature-only carver is not acceptable (ADR-0003).

2. **Write the failing unit test first (TDD).** In
   `tests/unit/carve/formats/<Format>CarverTest.cpp`, assert:
   - a valid sample → exact length and `Valid`;
   - the valid sample followed by trailing garbage → extent stops at the real end (the
     anti-false-positive case);
   - a truncated sample → bounded result, no overrun;
   - non-matching bytes → `Rejected`.

3. **Implement the carver.** Create
   `src/carve/formats/<Format>Carver.{hpp,cpp}` implementing `FormatCarver` in namespace
   `revenant::carve`:
   - `signatures()` returns the magic pattern(s);
   - `carve(ByteReader&)` walks the structure and returns `CarveResult{length,
     confidence, extension}` or a typed error.
   Keep each concern in its own function (recognize, measure, validate) — do not put the
   whole parser in one function. Use `ByteReader` bounds-checked reads and the endian
   helpers; no `reinterpret_cast`, no unaligned dereference, no unbounded loop.

4. **Register it.** Add the carver to `CarverRegistry` construction so it participates in
   scanning. Confirm no signature clash misroutes another format.

5. **Add the fuzz target (mandatory).** Create
   `tests/fuzz/<Format>CarverFuzz.cpp` that feeds arbitrary bytes to `carve`. Invariant:
   any input yields a verdict or typed error — never a crash, hang, or OOB read. Wire it
   into the fuzz build. This is a merge gate, not optional.

6. **Run the gates locally.**
   ```bash
   cmake --build --preset debug --target format-check
   cmake --build --preset debug --target tidy
   cmake --build --preset debug --target guard-limits
   ctest --preset debug --output-on-failure
   ```
   Fix everything until clean.

7. **Update docs & changelog.**
   - Add the format to the table in `docs/architecture/carving-engine.md`.
   - Add an entry under `[Unreleased] → Added` in `CHANGELOG.md`
     (`feat(carve): add <FORMAT> validating carver`).

8. **Story self-audit.** Complete the story-level checklist in
   [`docs/code-quality.md`](../../docs/code-quality.md): one function/one thing, no
   duplication, typed errors, tests cover malformed inputs, fuzz target present.

## Definition of done

- Unit + golden + fuzz tests pass under ASan + UBSan on Windows and Linux.
- All quality gates green; each file ≤ 250 lines; each function ≤ 10 statements.
- The carver returns **exact** extents (validated against trailing-garbage tests).
- Docs and `CHANGELOG.md` updated; self-audit completed.

## Anti-patterns to reject

- "Collect until the next header / a size cap" — forbidden (ADR-0003).
- A single giant `carve()` doing recognition, measurement, and validation together.
- Emitting a low-confidence candidate as if it were a good file — return the verdict and
  let arbitration (ADR-0006) decide; do not force extraction here.
- Skipping the fuzz target because "the parser looks simple".
