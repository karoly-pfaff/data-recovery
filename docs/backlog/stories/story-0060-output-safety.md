<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0060: Output safety — `sanitizeOutputPath` + `boundedCount`

- Epic: [epic-m1-vertical-slice](../epic-m1-vertical-slice.md)
- Status: Done
- Size: M

## Goal

Deliver ADR-0009's two mandatory, tested rules as reusable primitives: `boundedCount`,
the single checkpoint every untrusted on-disk size/count passes through before it may
size an allocation or loop, and `sanitizeOutputPath`, the single choke point every
recovered-file output path passes through before any write. Neither rule exists yet
anywhere in the codebase; this story opens the `revenant::recovery` module (the first
consumer of the destination-write side of the architecture) and the core
`BoundedCount.hpp` header, so both are ready for the NTFS filename-decoding and
`RecoverySink` stories later in M1.

## Design references

- [ADR-0009: output safety — path confinement & bounded allocation](../../architecture/adr/adr-0009-output-safety.md)
- [SECURITY.md](../../../SECURITY.md)

## Acceptance criteria

- [x] `template <std::unsigned_integral T> Result<std::size_t> boundedCount(T untrusted,
      std::size_t bound) noexcept` in `include/revenant/core/BoundedCount.hpp`
      (header-only): `untrusted > bound` → `Error{.code = ErrorCode::kOutOfRange}`;
      otherwise the value as `std::size_t`. The comparison happens in `std::uint64_t`
      space so a `T` wider than `std::size_t` on paper can never wrap it.
- [x] `Result<std::filesystem::path> sanitizeOutputPath(const std::filesystem::path&
      destinationRoot, std::string_view relativeName)` in
      `include/revenant/recovery/OutputPath.hpp` / `src/recovery/OutputPath.cpp`
      (namespace `revenant::recovery`) — the single choke point; there is no other way
      to derive a recovered-file output path.
- [x] The pipeline: rejects NUL/control bytes (`< 0x20`, `0x7F`); rejects a leading
      separator (`/etc/x`, `\\server\share`) before splitting (splitting first would
      silently collapse it into a relative join instead of rejecting the escape); splits
      on both `/` and `\`; drops empty and `.` segments; rejects any `..` or
      drive/volume-prefix (`X:`) segment outright; neutralizes Windows-reserved
      basenames (`CON PRN AUX NUL COM1-9 LPT1-9`, matched case-insensitively against the
      segment's basename before its first dot, with or without an extension) by
      prefixing `_`; strips trailing dots/spaces per segment (a segment left empty by
      stripping becomes `_`); enforces `kMaxSegmentBytes = 240` per cleaned segment and
      `kMaxSegments = 64` total segments (both reuse `boundedCount` internally, remapped
      to `kInvalidArgument` — see Known issues); joins the cleaned segments under
      `destinationRoot` and verifies containment (`weakly_canonical(destinationRoot)`'s
      path elements are a prefix of the lexically-normal join's elements) as a
      belt-and-braces check over the by-construction guarantee. Every rejection reason
      reports `kInvalidArgument` — the caller only needs to know "this name is unusable."
- [x] Never allocates or loops on a size read from untrusted bytes without going through
      `boundedCount` first (this story's own segment/count bounds dogfood it; the guard
      itself is now available to every future on-disk-count consumer, e.g. NTFS
      attribute lengths).
- [x] A libFuzzer target (`OutputPathFuzz.cpp`) exercises `sanitizeOutputPath` over
      arbitrary bytes against a fixed root, asserting containment internally (CI-built;
      MSVC has no local `clang++` — see Known issues).

## Test plan

- Unit (`BoundedCountTest.cpp`): under bound, at bound (exact), over bound, zero bound
  rejecting any positive count, zero bound accepting zero, and a wide (`uint64_t`)
  untrusted value compared safely against a narrow bound.
- Unit (`OutputPathTest.cpp`), all asserting the binding containment property
  (`result.value().string().starts_with(root.string())`) on every accepted case:
  - Rejected: `../../etc/passwd` (traversal), `/etc/x` (Unix absolute),
    `C:\evil` (drive prefix), `\\server\share` (UNC/leading separator), an embedded NUL
    byte, an embedded control byte (`0x01`), an all-dropped name (`"."`), and an empty
    name.
  - Neutralized: `CON.jpg` → `_CON.jpg`; `con` → `_con`; `com1.txt` →
    `_com1.txt` (case-insensitive, with extension); `CONX` passes unchanged (basename
    mismatch); `CON ` → `_CON`, `NUL  ` → `_NUL`, `COM1 ` → `_COM1` (a reserved
    basename with no dot but one-or-more trailing spaces — the post-review regression
    class, see Known issues §7); `CON . .` → `_CON` (strip fully to the bare reserved
    name, then neutralize).
  - Cleaned: `name.` and `name ` both strip to `name`; nested `a/b/c.jpg` is preserved
    under root; `./a/./b.jpg` and `a//b.jpg` both drop their empty/`.` segments to
    `root/a/b.jpg`.
  - Bounds: a segment of exactly `kMaxSegmentBytes` is accepted, one byte over is
    rejected; a name decomposing into exactly `kMaxSegments` segments is accepted, one
    more is rejected.
- Fuzz (`OutputPathFuzz.cpp`): arbitrary bytes as `relativeName` against a fixed root —
  the result is either a typed error or a path lexically inside that root; a genuine
  escape aborts the fuzzer directly (`std::abort()`, no project assertion macro exists
  to depend on), keeping the triggering input in libFuzzer's crash corpus.

## Definition of Done

- [x] Acceptance criteria met; unit tests green under ASan + UBSan (`ctest --preset
      debug` → 112/112: the 81 pre-existing plus 31 new — 6 `BoundedCount.*`, 25
      `OutputPath.*`, the last 4 added post-review as regression tests for the
      trailing-space neutralization-bypass fix, Known issues §7). Fuzz target
      CI-built/run (no local Clang toolchain in this environment).
- [x] Every new/changed function does one thing at one abstraction level; see the
      decomposition recorded in the self-audit below.
- [x] Lint/format/duplication/file-length guards clean: `guard-limits` and
      `format-check` (debug preset) exit 0; `tidy` (release preset) exits 0 with zero
      errors; `jscpd@4.0.5 --min-lines 8 --threshold 0 src include tools` reports 0
      clones.
- [x] `CHANGELOG.md` updated under `[Unreleased]/Added`.
- [x] Story-level self-audit checklist completed (below).
- [x] Epic row linked (`docs/backlog/epic-m1-vertical-slice.md`).

## Known issues

1. **Bound violations are remapped from `kOutOfRange` to `kInvalidArgument`.**
   `boundedCount` itself returns `kOutOfRange` on a bound violation (its own binding
   contract). `sanitizeOutputPath` reuses `boundedCount` for both the per-segment length
   bound and the total-segment-count bound, but deliberately does not let that error code
   leak through: from `sanitizeOutputPath`'s own contract, a segment or a name that is too
   long/too deep means exactly the same thing as a traversal or a forbidden byte — "this
   `relativeName` cannot be used" — so every rejection reason is normalized to
   `kInvalidArgument`, matching the one error code the brief pins explicitly ("empty
   result after cleaning → `kInvalidArgument`"). `boundedCount` itself keeps its own
   `kOutOfRange` semantics unchanged for direct callers (e.g. a future MFT
   attribute-length check).
2. **Containment check canonicalizes only `destinationRoot`, not the joined candidate**,
   per the brief's literal pipeline spec ("`weakly_canonical(root)` prefix over the
   lexically-normal join"). This means `sanitizeOutputPath` only proves containment
   correctly when `destinationRoot` is itself an absolute (or at least already-anchored)
   path — documented on the interface. In real usage `destinationRoot` is always the
   tool's resolved destination directory, so this holds; it is called out here because a
   relative `destinationRoot` would make the belt-and-braces check spuriously fail (a
   safe join would be rejected), not spuriously pass — fails closed, not open.
3. **The internal containment check compares path *elements*, not raw strings.** The
   brief's own test-level property assert is a plain `.string().starts_with(...)`
   (deliberately, per the brief: "pick one, use it consistently in tests and the fuzz
   target"). The production `isContainedWithin` helper instead walks `std::filesystem::path`
   iterators component-by-component — a plain string-prefix check would wrongly treat a
   sibling directory (`.../out-evil`) as "contained" within `.../out`. This is strictly
   stronger than what the tests assert, never weaker, and every binding test case still
   passes under it (segments are built via `root / segment / ...`, so a byte-prefix
   collision like `out`/`out-evil` can never actually arise from this pipeline's own
   joins — the two checks agree on every reachable input).
4. **`OutputPathSegment.{hpp,cpp}` is an internal, non-public pair** under
   `src/recovery/`, the same "internal to the pipeline, not a public interface" pattern
   `WindowMatch.hpp` established for `SignatureScanner` (story-0014) and
   `JpegMarkerWalk.hpp`/`JpegEntropyScan.hpp` established for `JpegCarver` (story-0010):
   it owns "turn one raw segment into a cleaned one, or reject/drop it," while
   `OutputPath.cpp` owns "assemble the cleaned segments under the root and verify
   containment." Quoted, same-directory `#include "OutputPathSegment.hpp"` from
   `OutputPath.cpp` — no extra CMake include-directory wiring needed, same as the
   `WindowMatch.hpp` precedent.
5. Test roots (`OutputPathTest.cpp`, `OutputPathFuzz.cpp`) use
   `std::filesystem::temp_directory_path() / "revenant-output-path-{test,fuzz}-root"`
   rather than a hard-coded absolute path — the latter would need a drive letter on
   Windows and wouldn't parse the same way on Linux CI, and `sanitizeOutputPath` never
   requires the destination tree to actually exist (`weakly_canonical` degrades
   gracefully to a lexical join once it runs out of existing ancestors).
6. **The release-preset `tidy` run drove a further decomposition pass beyond the first
   green implementation**, the same "tidy is the authority — split, never suppress"
   instruction story-0014 documented. The first draft compiled and passed all 108 tests
   but the full `clang-tidy` run (`readability-function-size`, `StatementThreshold: 10`,
   which — as story-0014 first found — counts every statement recursively, including
   ones contributed by macro-expanded code inside a function's own body) flagged real
   findings, all fixed on this branch before any commit:
   - `sanitizeOutputPath` (11 statements) — split out `isUsableRawName` (the two
     raw-byte rejections) and `confineNonEmptySegments` (the empty-result rejection),
     leaving `sanitizeOutputPath` a 3-step dispatch.
   - `splitRawSegments` (12 statements) — extracted `segmentEnd` (find one segment's end
     via `find_first_of`, replacing manual index-walking) as its own function.
   - `collectSegments` (12 statements) — split into `foldSegments` (the accumulation
     loop) + `checkSegmentCount` (unchanged), and the loop body itself split further into
     `classifyRawSegment` (structural decision only, returning a `Disposition` enum) +
     `keepCleanedSegment` (clean-and-append for the kKeep case) + `absorbRawSegment`
     (dispatch between them) — replacing the original single `processRawSegment`
     returning `Result<std::optional<std::string>>`, which packed classification and
     cleaning into one function.
   - `cppcoreguidelines-pro-bounds-avoid-unchecked-container-access` on
     `name[pos]`/`segment[1]` (raw `operator[]` on a `string_view`) — the `name[pos]`
     sites disappeared with the `segmentEnd`/`find_first_of` rewrite; `segment[1]`
     became `segment.at(1)` (bounds-checked; provably never throws here since
     `looksLikeDrivePrefix` only reaches it after confirming `segment.size() >= 2`).
   - `performance-enum-size` on the new `Disposition` enum — given an explicit
     `: std::uint8_t` base, matching the existing `Confidence : std::uint8_t` precedent.
   - `readability-identifier-naming` on a function-local `static const std::filesystem::path
     root` in both `testRoot()`/`fuzzRoot()` — clang-tidy's naming check classifies a
     function-local variable with *static* storage duration under `StaticConstantCase`
     (`kPascalCase`), not `LocalConstantCase` (`camelBack`, the ordinary-local-const rule
     story-0003 already established) — renamed to `kRoot` in both.
   - `misc-unused-using-decls` on `using revenant::Error;` in `OutputPathTest.cpp` — the
     test file only ever reads `.error().code`, never spells the bare `Error` type;
     removed (and, while auditing the same block, `using revenant::Result;` was also
     unused after the test-helper-function removal below and was removed too).
   - `modernize-raw-string-literal` on the UNC-path test literal — `"\\\\server\\share"`
     became `R"(\\server\share)"`.
   - **Two test-only helper functions (`expectContainedUnderRoot`, `expectRejected`)
     were removed entirely**, not just shrunk — this is the one finding that changed the
     test file's shape rather than a single expression. Both were flagged at 14
     statements each despite having only two `ASSERT_*`/`EXPECT_*` lines in their body;
     GoogleTest's `TEST(...)` macro expands each test into a `TestBody()` override whose
     declaration itself originates inside a macro, which `clang-tidy` skips analyzing —
     but a *hand-written* function that merely *calls* `ASSERT_TRUE`/`EXPECT_TRUE`
     internally is not exempt, and those macros expand into several statements each
     (a `switch`/`if`/`else` construct). No test file elsewhere in this codebase
     factors GTest assertions into a shared non-`TEST` helper for exactly this reason;
     every check is now inlined directly into each `TEST(...)` body instead, matching
     the existing convention throughout `tests/unit/`. This is a behavior-preserving
     mechanical change confirmed by re-running the full suite after each step (108/108
     green throughout).
   Every fix was verified not to weaken a single binding test case: the full 108/108
   `ctest --preset debug` suite (including all 27 `BoundedCount.*`/`OutputPath.*` tests
   added by this story) stayed green through every step above, confirmed by re-running
   after each batch of fixes, not only at the end. Final `tidy` transcript in
   task-1-report.md.
7. **Post-review Critical fix: reserved-name neutralization bypass via trailing
   space(s) with no dot.** A security review of the merged-to-branch code found that
   `cleanSegment` composed `stripTrailingDotsAndSpaces(neutralizeReservedName(...))` —
   neutralize, then strip — so a name like `"CON "` (4 bytes, no dot) reached
   `neutralizeReservedName` first: its basename extraction (`substr(0, find('.'))`,
   no dot present) is the *entire* 4-byte string `"CON "`, which never equals the
   3-byte `"CON"` under the equal-length case-insensitive compare in
   `equalsIgnoreCase` — so it's judged *not* reserved and passed through unchanged,
   and only then does `stripTrailingDotsAndSpaces` peel the trailing space back off,
   yielding the bare reserved device name `"CON"` on disk. A direct ADR-0009 rule-2
   violation (an attacker-controlled name maps to a Windows-reserved special file
   after "sanitization"). **Fix**: reordered the composition to
   `neutralizeReservedName(stripTrailingDotsAndSpaces(...))` — strip first, so the
   basename `neutralizeReservedName` sees is always the already-trimmed form. Traced
   against every previously-passing case to confirm no regression: `"CON."` and
   `"CONX"` are unaffected (strip is a no-op for both — `"CON."`'s dot isn't literally
   trailing until stripped, but stripping `"CON."` removes the trailing dot yielding
   `"CON"` before neutralization either way, same final `"_CON"`); `"COM9.tar.gz"` is
   unaffected (no trailing dot/space, strip is a no-op, neutralize sees the same
   `"COM9.tar.gz"` either order); `"...."` is unaffected (strips fully to empty →
   `"_"` → not reserved, same result both orders). The empty-fallback interaction
   (stripping an all-dot/all-space segment to nothing substitutes `"_"` *before*
   neutralization now runs) was re-verified: `"_"` is 1 byte, matches no reserved
   name (all 3–4 bytes), so it passes through unchanged — correct. Four regression
   tests added, each retaining the containment property assert:
   `ReservedNameWithTrailingSpaceAndNoDotIsNeutralized` (`"CON "` → `"_CON"`),
   `ReservedNameWithMultipleTrailingSpacesIsNeutralized` (`"NUL  "` → `"_NUL"`),
   `ReservedComPortNameWithTrailingSpaceIsNeutralized` (`"COM1 "` → `"_COM1"`), and
   `ReservedNameSurvivesStrippingThenNeutralization` (`"CON . ."` — dots and spaces
   interleaved, strips fully down to `"CON"` before neutralization → `"_CON"`).
   Confirmed each fails under the pre-fix ordering by hand-trace (not re-verified by
   reverting the fix, to avoid landing a known-bad state even transiently) and passes
   under the fix; full suite re-run green (112/112) after the change. Targeted
   `clang-tidy` (the two touched files, `-p build/release`) exits 0 with zero errors;
   the full-glob `tidy` run (all 78 files, release preset) was re-run to confirm and
   also exits 0 with zero errors (it took long enough this session to need a
   background poll with `Get-Process` CPU-time checks confirming it was genuinely
   running, not hung, before it finally completed).

## Story-level self-audit (docs/code-quality.md)

### Responsibility & clarity
- [x] Every new/changed function does exactly one thing at one abstraction level (final
      shape, after the tidy-driven decomposition recorded in Known issues §6):
      `boundedCount` (one comparison); `isForbiddenByte`/`hasForbiddenBytes` (byte-class
      check); `startsWithSeparator`/`isUsableRawName` (the two raw-byte rejections);
      `segmentEnd` (find where one segment ends); `splitRawSegments` (fold `segmentEnd`
      into the full split); `looksLikeDrivePrefix`/`equalsIgnoreCase`/`isReservedBasename`
      (one predicate each); `neutralizeReservedName`/`stripTrailingDotsAndSpaces` (one
      transform each); `cleanSegment` (compose the two transforms + the length bound);
      `classifyRawSegment` (structural classification only: drop/reject/keep, returning
      the `Disposition` enum — no cleaning); `keepCleanedSegment` (clean an
      already-classified-kKeep segment and append it); `absorbRawSegment` (classify, then
      dispatch to drop/keep for one raw segment); `foldSegments` (the accumulation loop);
      `checkSegmentCount`/`collectSegments` (the total-count bound, then the two-step
      fold-then-bound pipeline); `assembleUnder` (one join); `isContainedWithin` (one
      component-wise comparison); `confineToRoot` (join + verify containment);
      `confineNonEmptySegments` (the empty-result rejection); `sanitizeOutputPath` (the
      three-step top-level pipeline: reject raw bytes, collect segments, confine).
- [x] Each function's purpose is understood from its name and signature alone.
- [x] `OutputPathSegment.{hpp,cpp}` (segment-level cleaning) and `OutputPath.{hpp,cpp}`
      (assembly, containment, the public entry point) are each focused on one
      responsibility — split for the same reason `WindowMatch.hpp`/`JpegMarkerWalk.hpp`
      were: two genuinely separate sub-problems, not just a line-count dodge (both files
      are well under the 250-line cap even combined; the split is by responsibility).

### Design
- [x] `boundedCount` has one reason to change: what "a count exceeds its bound" means.
      `sanitizeOutputPath`/the segment pipeline have one reason to change each: what
      "a safe destination path" means vs. what "a safe segment" means.
- [x] No new type introduced needs DIP — both primitives are free functions over
      value types (`std::filesystem::path`, `std::string`), matching `ByteReader`'s
      "leaf" primitives; nothing here depends on a concrete device or filesystem.
- [x] YAGNI: no configurable reserved-name list, no pluggable separator set, no
      alternate confinement policy — only the one pipeline ADR-0009 specifies. The
      `kMaxSegmentBytes`/`kMaxSegments` constants are public (not hidden) specifically
      because the test plan needs to assert *at* the bound without hard-coding magic
      numbers a second time.
- [x] No duplicated knowledge: `boundedCount` is the one place "compare an untrusted
      value against a bound safely" lives; `sanitizeOutputPath`'s own two bound checks
      call it rather than re-implementing the comparison.

### Anti-patterns
- [x] No God object — segment cleaning, path assembly, and containment verification are
      three separate, small, single-purpose functions/files, not one function doing all
      three.
- [x] No boolean-parameter traps; `classifyRawSegment`'s three-way outcome (typed error /
      `Disposition::kSkip` / `Disposition::kKeep`) reads at each call site, never a bare
      bool return or a bool parameter standing in for "drop vs. keep."
- [x] No premature generality, dead code, or commented-out code.

### Correctness & safety
- [x] Every fallible step returns `Result<T>`; every caller checks `hasValue()` before
      `.value()` — no throw, no swallowed error, no silent fallback.
- [x] Read-only: neither `boundedCount` nor `sanitizeOutputPath` touches any file's
      *contents*; `sanitizeOutputPath` only computes a path value — no directory is
      created, no file is opened, no write occurs anywhere in this story. The one
      filesystem-touching call (`std::filesystem::weakly_canonical`) only *queries*
      `destinationRoot`'s existing ancestors; it degrades to a pure lexical join for the
      (expected) not-yet-existing parts, so it never depends on the destination tree
      having been created.
- [x] No UB in byte handling: no `reinterpret_cast` anywhere in this story (the fuzz
      target converts each `std::uint8_t` to `char` by value via `static_cast`, the same
      per-element-conversion idiom `JpegCarverFuzz.cpp`/`ByteReaderFuzz.cpp` already use,
      never a pointer-punning cast); no unaligned deref; no signed-overflow-prone
      arithmetic (`boundedCount`'s comparison is explicitly widened to `std::uint64_t`
      before comparing).

### Tests
- [x] Written test-first: RED captured (`Cannot open include file:
      'revenant/core/BoundedCount.hpp'` / `'revenant/recovery/OutputPath.hpp'`) before
      either header existed; GREEN after the implementation (full transcripts in
      task-1-report.md).
- [x] Tests cover malformed/edge inputs by name: every traversal/absolute/drive/UNC
      form the brief lists, both control-byte classes (embedded NUL and a plain control
      byte), every reserved-name shape (with/without extension, case-insensitivity, the
      non-matching near-miss `CONX`, and — added post-review — the trailing-space-with-
      no-dot shape that bypassed neutralization before the Known issues §7 fix), both
      trailing-character strip cases, both dot/empty-segment-dropping shapes, and both
      bounds at their exact edge (at vs. one over) for both the per-segment and
      total-segment-count limits.
- [x] `sanitizeOutputPath` — the one byte-parsing entry point this story adds that
      consumes untrusted, attacker-influenced bytes — has a dedicated fuzz target
      (`OutputPathFuzz.cpp`) that asserts the containment invariant internally.

## Notes

- **Unicode superscript-digit `COM`/`LPT` variants are out of scope for this story.**
  A security review flagged that Windows also treats `COM¹`/`COM²`/`COM³` (superscript
  digits U+00B9/U+00B2/U+00B3, not the ASCII digits `1`/`2`/`3`) as aliases for the
  same reserved device names on some Windows versions/APIs. This story's reserved-name
  table (`kReservedBasenames`) is ASCII-only, matching the brief's binding list
  (`CON PRN AUX NUL COM1-9 LPT1-9`) — no test in this story's plan exercises the
  superscript-digit forms, and adding Unicode-aware basename comparison is a
  meaningfully bigger change (decoding UTF-8 code points, not just byte-for-byte
  case-folding) than this story's scope. Recorded here as backlog material for a
  follow-up story rather than fixed silently in this branch.
