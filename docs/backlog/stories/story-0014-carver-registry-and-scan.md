<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0014: `CarverRegistry` + streaming signature scan

- Epic: [epic-m1-vertical-slice](../epic-m1-vertical-slice.md)
- Status: Done
- Size: M

## Goal

The carve layer's permanent seams: the `FormatCarver` interface, the registry that owns
carvers, and the streaming signature scanner that turns a raw `BlockDevice` into
verdict-carrying candidates — discovery only, never extraction (ADR-0006).

## Design references

- [Carving engine](../../architecture/carving-engine.md)
- [ADR-0003: validating carving](../../architecture/adr/adr-0003-validating-carving.md)
- [ADR-0006: candidate arbitration & deferred extraction](../../architecture/adr/adr-0006-candidate-arbitration-deferred-extraction.md)

## Acceptance criteria

- [x] Interface types exactly as specified: `Confidence`, `carve::Signature`,
      `carve::CarveResult`, `carve::FormatCarver`, `carve::CarverRegistry`,
      `carve::ScanCandidate`, `carve::CandidateVisitor`, `carve::ScanConfig`,
      `carve::ScanStats`, `carve::SignatureScanner`.
- [x] `CarverRegistry` owns registered carvers and exposes the combined maximum
      signature span (longest magic + in-file offset) for window-overlap sizing.
- [x] `SignatureScanner` streams a `BlockDevice` in bounded windows with cross-window
      overlap so no match is ever lost at a window boundary.
- [x] At each surviving match, the scanner invokes `carve()` on a bounded carve window
      and reports every verdict to the `CandidateVisitor` — no candidate is dropped or
      duplicated.
- [x] The scanner resumes past a Valid/Uncertain extent (no rescan inside it) and one
      byte past a Rejected match.
- [x] The scanner never allocates from untrusted sizes — window and carve-buffer sizes
      come only from `ScanConfig`, never from device contents.
- [x] A libFuzzer target exercises the scan loop end to end (CI-built/run; MSVC has no
      clang++ locally, so this target is not built in this session — see Known issues).

## Test plan

- Unit (`CarverRegistryTest.cpp`): starts empty; owns registered carvers; reports the
  widest signature span across carvers.
- Unit (`SignatureScannerTest.cpp`), all via `FakeCarver`/`CollectingVisitor`: match at
  offset 0 and mid-device; resume past a Valid extent without rescanning inside it
  (no duplicate candidate); a Rejected match advances by exactly one byte (a second,
  adjacent-but-non-overlapping match is reachable only because the resume step is
  exactly `+1`, not the verdict's length); a match straddling a window boundary is
  still found whole; a match at the device end yields a candidate bounded to the bytes
  actually available; an empty device and a device smaller than one signature produce
  no candidates and no error; `ScanStats.bytesScanned` reports the device size.
- Fuzz (`SignatureScanFuzz.cpp`): random device bytes through `scan()` with a
  fixed-signature inline carver whose returned length is itself derived from the input
  (exercises resume-cursor arithmetic); the invariant is "never crash, hang, or read out
  of bounds" — never a specific candidate set.

## Definition of Done

- [x] Acceptance criteria met; unit tests green under ASan+UBSan (`ctest --preset debug`
      → 68/68, the 58 pre-existing plus 10 new: 3 `CarverRegistry.*`, 7
      `SignatureScanner.*`). Fuzz target CI-built/run (see Known issues).
- [x] Every new/changed function does one thing at one abstraction level;
      `SignatureScanner::scanOneWindow` (and the rest of the private scan pipeline)
      decomposed per the Prime Directive — see Known issues for the exact shape and
      why it differs from the task brief's illustrative sketch.
- [x] Coverage not separately re-measured this session (same scope limitation as
      stories 0003–0007: the `coverage` preset was not run); all new production code is
      exercised by the 10 new unit tests plus the fuzz target.
- [x] Lint/format/duplication/file-length guards clean: `guard-limits` and
      `format-check` (debug preset) exit 0; `tidy` (release preset) exits 0 with zero
      errors across all 59 tidied files; `jscpd@4.0.5 --min-lines 8 --threshold 0 src
      include tools` reports 0 clones.
- [x] `CHANGELOG.md` updated under `[Unreleased]/Added`.
- [x] Story-level self-audit checklist completed (below).

## Known issues

Several small, documented deviations from the task brief's illustrative code — all
mechanical, tooling-driven, and verified not to change any observable behavior (the
seven pinned `SignatureScanner` tests plus the three `CarverRegistry` tests stayed
green, unweakened, through every fix below):

1. **`scanOneWindow`'s decomposition is deeper than the brief's sketch, and reshapes
   `attemptCarve`/`scan` too.** The brief gave `attemptCarve` and `scan` as "complete,
   given" code and asked only for `scanOneWindow`'s body, targeting "≤ 8 statements."
   The release-preset `tidy` run (clang-tidy 22, `readability-function-size`,
   `StatementThreshold: 10`) counts every statement including each nested compound
   block, which is stricter than a hand count — it flagged `attemptCarve` (11
   statements), `scan` (12), and my first-pass `scanOneWindow` (13, plus "6 parameters,
   threshold 5") and `processMatches` (15) as originally written. Per the task's own
   instruction ("if the release-preset tidy run flags `readability-function-size`
   anywhere, split further, never suppress"), all four were decomposed further:
   - `attemptCarve` split into `attemptCarve` + `buildCandidate` (carve-and-wrap).
   - The match loop split into `processMatches` (the loop) + `applyMatch` (one match:
     skip-if-covered, else carve, report, advance).
   - `scanOneWindow` split into `scanOneWindow` (read + delegate) + `stepWindow`
     (terminate-on-empty-read, else process matches and compute the next step) +
     `advanceWindow` (run one window, fold its count into `ScanStats`) + `runScanLoop`
     (the device-spanning while loop) — `scan()` itself is now just buffer setup +
     `runScanLoop`.
   - `scanOneWindow`'s 6-parameter/swappable-adjacent-buffers finding (`windowBuffer`
     and `carveBuffer`, both `std::vector<std::byte>&`, flagged by
     `bugprone-easily-swappable-parameters`) is fixed by bundling both into a private
     `ScanBuffers` struct — this is why `scan()`'s body no longer matches the brief's
     two separate `std::vector` locals verbatim.
   - Every one of these still returns/threads exactly the values the brief's NOTE
     specifies (next cursor = `max(windowEnd - overlap, resumeCursor)`, clamped to
     `windowEnd` when that would not strictly advance past `cursor`; empty read →
     `device.sizeInBytes()`); only the internal call shape changed, all private.
2. **`Match`/`WindowMatches`/the window-reading and signature-matching functions moved
   to a new pair of files, `src/carve/WindowMatch.hpp` + `src/carve/WindowMatch.cpp`.**
   After the above decomposition and a `clang-format` pass (which wraps several
   multi-parameter signatures across more lines), and after the
   `misc-include-cleaner`-driven include work above, `SignatureScanner.cpp` grew to
   280 lines — over the file-length guard's 250-line hard cap. Split by responsibility
   (mirroring the `ReadRange.hpp`/`ImageFileDeviceShared.cpp` precedent from story-0002):
   `WindowMatch.{hpp,cpp}` owns "read a window off the device and find every signature
   match in it" (`Match`, `WindowMatches`, `readWindow`, `readAndMatch`); `SignatureScanner.cpp`
   (now 183 lines) keeps "carve a match and drive the scan loop." `readWindow` is
   exposed from the header (not anonymous-namespaced) because both files call it
   (`attemptCarve` reads one carve-bounded window; `readAndMatch` reads one scan
   window). **Post-review correction**: this pair first landed under
   `include/revenant/carve/`, which leaked internal scan-loop plumbing into the public
   header surface alongside the story's produced interfaces (review finding). Moved to
   `src/carve/WindowMatch.hpp` instead — same-directory quoted `#include "WindowMatch.hpp"`
   from `SignatureScanner.cpp`/`WindowMatch.cpp` (no CMake change needed; the quoted-include
   search already checks the including file's own directory first). The public
   `include/revenant/carve/SignatureScanner.hpp` no longer includes it at all — it only
   forward-declares `struct WindowMatches;` (sufficient for a reference parameter on a
   declaration), so the full type is visible only where it's actually used (the two
   `src/carve/*.cpp` files). `WindowMatch.hpp` now carries a header comment stating it is
   internal to the scan loop, not a public interface, and subject to change; precedent:
   concrete carvers also live under `src/carve/` (Task 2 of this plan). The tidy glob's
   `src/*.hpp` pattern already covers it (confirmed: file 24/59 in the tidy transcript,
   zero findings), and `cmake/DevTargets.cmake`'s off-platform filters
   (`Posix\.cpp$`/`Windows\.cpp$`) don't match it — no CMake wiring changed.
3. **`stepWindow` made `static`.** `readability-convert-member-functions-to-static`:
   it only touches its parameters (never `registry_`/`config_`), so clang-tidy flagged
   it; marked `static` (dropping `const`, which doesn't apply to `static` members).
4. **MSVC's legacy (non-conformant) preprocessor and the brief's inline
   `SignatureScanner{registry, ScanConfig{}}.scan(...)` construct inside `ASSERT_TRUE`.**
   Six call sites in the brief's own `SignatureScannerTest.cpp` build the scanner via
   brace-init directly inside an `ASSERT_TRUE(...)`/`.scan(...).hasValue()` expression;
   MSVC's default (non-`/Zc:preprocessor`) macro expansion splits on the comma inside
   `{registry, ScanConfig{}}` as if it were a second macro argument
   (`C2220`/`C4002: too many arguments for function-like macro invocation 'ASSERT_TRUE'`).
   Fixed with the minimal, semantics-identical accommodation: one extra pair of parens
   around the whole boolean expression at each of the six sites
   (`ASSERT_TRUE((SignatureScanner{...}.scan(...).hasValue()));`) — same precedent class
   as this task brief's own "apply the minimal semantics-identical accommodation" note.
   Confirmed by the RED capture (evidence in task-1-report.md): the six-parens fix alone
   took the file from a wall of `C2187`/`C2958`/`C2440` syntax errors to a clean compile.
5. **`RejectedMatchAdvancesOneByte`'s byte-planting was mathematically unsatisfiable as
   given, and my first fix lost discriminating power (review finding — confirmed by
   the reviewer as a genuine defect in the plan's own test data).** The brief's literal
   test plants a 2-byte magic (`0xAB 0xCD`) at offset 50 and again at offset 51 — but
   offset 51 overwrites byte 51 (the second byte of the offset-50 magic) from `0xCD` to
   `0xAB`, so the buffer only ever contains **one** valid 2-byte match (at 51; offset 50
   becomes `(0xAB, 0xAB)`, not a match). No `FormatCarver`/scanner implementation —
   correct or not — can report two candidates from one raw match; verified empirically
   (the unmodified test produced exactly 1 candidate against the finished, gate-clean
   implementation, not 2). My first fix (moving the second plant to offset 52, adjacent
   but non-overlapping) made the test satisfiable but **not discriminating**: with
   `FakeCarver`'s length forced to 0 for the Rejected case, a correct `+1` resume
   (→ 51) and a buggy `+2` (signature-width) resume (→ 52) both land at-or-before 52,
   so both would report 2 candidates — the test could no longer catch a `+2` bug.
   **Corrected fix**: restored genuine overlap with a *self-overlapping* magic. A
   non-palindromic 2-byte magic (`{0xAB, 0xCD}`) can never have two raw matches exactly
   one byte apart — the shared byte would need to equal both `magic[0]` and `magic[1]`
   at once — so `FakeCarver` (fixed at `{0xAB, 0xCD}`, given code, left untouched)
   cannot express this scenario at all. Added a minimal test-local double,
   `OverlappingRejectCarver` (in `SignatureScannerTest.cpp`'s anonymous namespace, not
   `FakeCarver.hpp`, to keep the shared support double's given behavior/signature
   exactly as the brief wrote it), with magic `{0xAB, 0xAB}` and a `carve()` fixed to
   `Confidence::kRejected`/length 0. The test now plants three consecutive `0xAB` bytes
   at offsets 50, 51, 52: raw `{0xAB, 0xAB}` matches exist at **both** 50 and 51
   (sharing byte 51 — genuine overlap), asserting `candidates().size() == 2U` with
   offsets exactly 50 then 51. Traced against the finished implementation: window scan
   (single 4 MB window, whole 1024-byte device) finds `matches = [50, 51]` sorted
   ascending, both `>= cursor(0)`; `applyMatch` processes 50 (`resumeCursor` 0 → not
   skipped), carves (Rejected/length 0), reports it, and `resumeOffset` advances
   `resumeCursor` to exactly `50 + 1 = 51`; checking match 51 against `outcome.resumeCursor`
   uses strict `<` (`match.offset < outcome.resumeCursor`), so `51 < 51` is false — **not**
   skipped — carved and reported too. Result: exactly 2 candidates, offsets 50 and 51,
   confirmed by `ctest -R SignatureScanner.RejectedMatchAdvancesOneByte`. A buggy
   `+2`/length-derived resume would instead set `resumeCursor = 52`, making
   `51 < 52` true — match 51 would be skipped and only 1 candidate reported — so this
   version of the test now genuinely discriminates the two behaviors again, as
   originally intended.
6. **Fuzz file (`SignatureScanFuzz.cpp`), tidy-hygiene only (CI builds/runs it; MSVC has
   no `clang++` here, per the task's environment note).** Three mechanical fixes to
   pass the release-preset `tidy` run: (a) `misc-include-cleaner` — added direct
   includes for symbols only picked up transitively (`revenant/core/Confidence.hpp`,
   `revenant/carve/Signature.hpp`, etc., across `FakeCarver.hpp` and this file); (b)
   `cppcoreguidelines-pro-bounds-pointer-arithmetic` on the brief's
   `std::transform(data, data + size, ...)` — replaced with the exact
   `toByteVector(std::span<const std::uint8_t>{data, size})` +
   `std::ranges::transform` pattern already used by `ByteReaderFuzz.cpp` (story-0003),
   zero behavior change; (c) `readability-function-size` on
   `LLVMFuzzerTestOneInput` (11 statements) — fixed by the same `toByteVector`
   extraction, which also halves this function's statement count; (d)
   `misc-const-correctness` on the local `config` — made `const`.
7. **Default member initializers added to `ScanCandidate::offset` and
   `Signature::offset`.** `cppcoreguidelines-pro-type-member-init` flagged both
   (aggregates with a mix of self-initializing members — `CarveResult`/`span` — and a
   bare scalar). Fixed with `= 0` on each, mirroring the existing `Error::offset`
   precedent in `revenant/core/Error.hpp`. Field order, aggregate-ness, and every
   existing designated-init call site are unchanged.
8. **`modernize-use-ranges`**: `appendMatches`'s two `std::search` calls (window-wide,
   then restarting `it + 1`) rewritten as `std::ranges::search`/`std::ranges::subrange`,
   same two-pass overlapping-match algorithm, same result set.
9. **`misc-include-cleaner`, direct includes (story-0002/0004/0005/0007 precedent).**
   Added includes for symbols used but only picked up transitively: `<cstddef>`,
   `<memory>`, `<span>`, `revenant/carve/FormatCarver.hpp`, `revenant/carve/Signature.hpp`
   to `CarverRegistry.cpp`; `revenant/core/Result.hpp`, `revenant/core/io/BlockDevice.hpp`
   to `SignatureScanner.cpp`; `revenant/carve/{FormatCarver,CarverRegistry}.hpp`,
   `revenant/core/Result.hpp`, `revenant/core/io/BlockDevice.hpp` to `WindowMatch.cpp`;
   `revenant/carve/ScanCandidate.hpp` to `CollectingVisitor.hpp`; `<algorithm>`,
   `revenant/core/{Confidence,Result,ByteReader}.hpp`, `revenant/carve/{Signature,CarveResult}.hpp`
   to `FakeCarver.hpp` (and removed its unused `<string>`); `<cstdint>` to
   `SignatureScannerTest.cpp`, plus (post-review, for the new `OverlappingRejectCarver`
   double) `<array>`, `<span>`, `revenant/carve/{CarveResult,FormatCarver,Signature}.hpp`,
   `revenant/core/{ByteReader,Result}.hpp`.
10. **`readability-named-parameter` on `OverlappingRejectCarver::carve`'s unused
    `ByteReader&`.** Named the parameter (`reader`) and discarded it explicitly —
    `static_cast<void>(reader);` — matching the existing `NullVisitor::onCandidate`
    convention in `SignatureScanFuzz.cpp` for an intentionally-unused override parameter.

Verification transcript (`cmake --build --preset release --target tidy`, final run
after the post-review fixes above, including `WindowMatch.hpp`'s relocation to
`src/carve/` — file 24/59 in the transcript, zero findings): all 59 tidied files
processed, `Suppressed 323800 warnings (323758 in non-user code, 42 NOLINT)`,
**0 errors**, exit code 0. Full transcripts in task-1-report.md.

## Story-level self-audit (docs/code-quality.md)

- Responsibility & clarity: yes — every function in the final decomposition does one
  thing (`readWindow` reads; `matchesInWindow`/`appendMatches` find raw signature hits;
  `overlapBytes` computes one number; `readAndMatch` combines "read + find + filter to
  cursor" as one window-reading step; `buildCandidate`/`attemptCarve` run one carver
  over one bounded read; `resumeOffset` computes one number; `applyMatch` decides
  skip-vs-carve for one match; `processMatches` folds `applyMatch` over a list;
  `nextCursor` computes one number; `stepWindow`/`scanOneWindow`/`advanceWindow`/
  `runScanLoop`/`scan` are the five layers of the scan loop, each owning exactly one
  step of it — terminate-or-process, read-then-delegate, run-and-fold-stats, drive-
  the-loop, and set-up-then-delegate, respectively). `WindowMatch.{hpp,cpp}` owns
  "read a window, find its matches"; `SignatureScanner.{hpp,cpp}` owns "carve a match,
  drive the loop" — two responsibilities the 250-line guard confirmed didn't belong in
  one file.
- Design: `CarverRegistry` has one reason to change (what "own the carvers and report
  the widest signature span" means); `SignatureScanner` has one reason to change (the
  window/overlap/resume algorithm). Both depend on the `FormatCarver`/`BlockDevice`/
  `CandidateVisitor` interfaces (DIP), never a concrete carver or device — proven by
  every test using `FakeCarver`/`InMemoryDevice`/`CollectingVisitor` test doubles with
  zero production-code changes needed. No speculative generality: no configurable
  match-search strategy, no pluggable resume policy — YAGNI, only what the tests need.
  `ScanBuffers`/`WindowStep`/`Match`/`WindowMatches`/`MatchOutcome` are the one place
  each piece of scan-loop state is threaded, so there's no duplicated knowledge of
  "what does a window's outcome look like."
- Anti-patterns: none introduced — no God object (registry vs. scanner vs. window-match
  plumbing are three separate translation units); no boolean-parameter traps
  (`Confidence` is a named 3-way enum, never a bool); nesting stays at 2 levels
  throughout; no dead or commented-out code.
- Correctness & safety: every fallible step returns `Result<T>` and every caller checks
  `hasValue()` before touching `.value()` — no throw, no swallowed error, no silent
  fallback. The source device is only ever read (`BlockDevice::readAt`, never a write
  member exists on the interface); this story adds no write path. Byte handling is
  UB-free: `std::span` throughout, no `reinterpret_cast`, `std::ranges::search` instead
  of raw pointer arithmetic, and window/carve-buffer sizes come only from `ScanConfig`
  constants — the scanner never allocates a size read from the device (ADR-0009), which
  the fuzz target exists specifically to keep honest under arbitrary input.
- Tests: written test-first — RED captured before any `revenant/carve/*` header
  existed (`Cannot open include file: 'revenant/carve/CarverRegistry.hpp'` /
  `'revenant/carve/SignatureScanner.hpp'`), GREEN after the implementation. All seven
  `SignatureScanner` tests target an edge case by name (offset-0/mid match, resume-
  dedup, reject-advance-by-one, window-straddle, device-end truncation, empty/tiny
  device, bytes-scanned reporting) — no happy-path-only coverage. The fuzz target
  (`SignatureScanFuzz.cpp`) is the fuzz coverage for the one byte-parsing entry point
  this story adds (the scan loop over untrusted device bytes); it is CI-built/run only
  (MSVC has no `clang++` in this dev environment) — same scope note as every prior
  fuzz-target story in this repo.
