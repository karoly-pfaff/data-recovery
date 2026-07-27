<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0061: NTFS filename decoding (UTF-16) + safe/disambiguated output names

- Epic: [epic-m1-vertical-slice](../epic-m1-vertical-slice.md)
- Status: Done
- Size: M

## Goal

Deliver ADR-0010's decode step and disambiguation step as reusable primitives:
`decodeUtf16Name`, which turns an on-disk NTFS UTF-16LE name into canonical UTF-8 with a
defined, lossless policy for undecodable content, and `disambiguate`, the deterministic
collision-suffixing step that keeps distinct recovered files from overwriting one
another at the destination. This story opens the `revenant::fs` module (the filesystem
layer's first seam — it "knows the encoding", per ADR-0010) and adds `disambiguate`
alongside story-0060's `sanitizeOutputPath` in `revenant::recovery` (the destination/sink
layer — it "knows the destination"). Both are ready for the NTFS `$FILE_NAME` attribute
parser and `RecoverySink` stories later in M1.

## Design references

- [ADR-0010: filename decoding & cross-platform-safe output](../../architecture/adr/adr-0010-filename-decoding-safe-output.md)
- [ADR-0009: output safety — path confinement & bounded allocation](../../architecture/adr/adr-0009-output-safety.md)
  (`disambiguate` is step 4 of the same pipeline `sanitizeOutputPath`, story-0060, opened)

## Acceptance criteria

- [x] `struct DecodedName { std::string utf8; bool lossless; }` and `[[nodiscard]]
      DecodedName decodeUtf16Name(std::span<const std::byte> utf16le)` in
      `include/revenant/fs/NameDecode.hpp` / `src/fs/NameDecode.cpp` (namespace
      `revenant::fs`). UTF-16LE code units decode to canonical UTF-8: a surrogate pair
      (high `D800`-`DBFF` immediately followed by low `DC00`-`DFFF`) becomes one 4-byte
      UTF-8 code point (`0x10000 + ((hi-D800)<<10) + (lo-DC00)`); every other BMP unit
      becomes 1/2/3 UTF-8 bytes by value. Undecodable content is escaped losslessly
      rather than dropped or substituted: an unpaired or reversed surrogate becomes the
      literal `%uXXXX` (uppercase hex, 4 digits), a dangling odd trailing byte becomes
      `%XX`, and a literal NUL code unit is *always* escaped as `%u0000` — it is never
      allowed to reach the output as a raw NUL byte, even though `sanitizeOutputPath`
      would reject one anyway (defense in depth: the decoder itself never emits one).
      `lossless` is false whenever any escape fires. Hand-rolled encoder (no
      `<codecvt>`, deprecated since C++17) built on the existing `fromLittleEndian`
      helper — no new byte-reading primitive needed.
- [x] `inline constexpr int kMaxDisambiguationAttempts = 10000;` and `[[nodiscard]]
      std::string disambiguate(std::string_view desired, const
      std::function<bool(std::string_view)>& taken)` in
      `include/revenant/recovery/Disambiguate.hpp` / `src/recovery/Disambiguate.cpp`
      (namespace `revenant::recovery`). `desired` unchanged if `taken(desired)` is
      false; otherwise tries `"name (2).ext"`, `"name (3).ext"`, ... — the suffix
      inserted before the LAST extension dot (appended outright for an extensionless
      name) — returning the first candidate `taken` reports free. Bounded at
      `kMaxDisambiguationAttempts` numbered candidates (`(2)` through
      `(kMaxDisambiguationAttempts + 1)`): if every one comes back taken, returns
      `desired + " (overflow)" + <counter>` unconditionally (no further `taken` calls),
      so the function always terminates regardless of `taken`'s behavior. `taken` is
      the caller's own "is this name already used" oracle — this story does not
      prescribe what backs it (a directory listing, an in-memory set, ...).
- [x] A libFuzzer target (`NameDecodeFuzz.cpp`) exercises `decodeUtf16Name` over
      arbitrary bytes, asserting internally (a small hand-rolled UTF-8 well-formedness
      checker) that the output is always valid UTF-8 — the binding invariant from
      ADR-0010 (CI-built; MSVC has no local `clang++`, see Known issues).

## Test plan

- Unit (`NameDecodeTest.cpp`): empty input; ASCII round-trip; a 2-byte BMP character
  (`é` U+00E9, `ő` U+0151 — both accented Latin letters, the story brief's own
  examples); a 3-byte BMP character (€ U+20AC); a surrogate pair decoding to 4-byte
  UTF-8 (𝄞 U+1D11E MUSICAL SYMBOL G CLEF, high `D834`/low `DD1E`); an unpaired high
  surrogate at end-of-input; a high surrogate followed by a non-surrogate code unit;
  a reversed pair (low surrogate with no preceding high); a dangling odd trailing byte
  after a valid unit, and as the sole byte of input; an embedded NUL code unit between
  two ordinary characters, with an explicit assertion that no raw NUL byte survives
  into the output.
- Unit (`DisambiguateTest.cpp`): a free name returned unchanged; a single collision
  inserting `" (2)"` before the LAST dot (`report.tar.gz` → `report.tar (2).gz`, proving
  multi-dot names split at the last one, not the first); two collisions advancing to
  `(3)`; an extensionless name appending the suffix outright; the bound pinned two ways
  — a `taken` lambda that counts numbered-candidate calls and returns free exactly on
  the `kMaxDisambiguationAttempts`-th one (proving the loop covers the full bound with
  no off-by-one) and a `taken` lambda that always reports every name taken (proving the
  unconditional overflow fallback and its exact format).
- Fuzz (`NameDecodeFuzz.cpp`): arbitrary bytes as raw UTF-16LE input — `decodeUtf16Name`
  never crashes and its `utf8` output is always well-formed UTF-8, checked by an
  internal validator (lead-byte shape plus continuation-byte count/well-formedness);
  a genuine malformed output aborts the fuzzer directly (`std::abort()`, the same
  no-project-assertion-macro convention `OutputPathFuzz.cpp` established).

## Definition of Done

- [x] Acceptance criteria met; unit tests green under ASan + UBSan (`ctest --preset
      debug` → 130/130: the 112 pre-existing plus 18 new — 12 `NameDecode.*`, 6
      `Disambiguate.*`). Fuzz target CI-built/run (no local Clang toolchain in this
      environment; tidy-checked locally instead, see Known issues).
- [x] Every new/changed function does one thing at one abstraction level; see the
      decomposition recorded in the self-audit below.
- [x] Lint/format/duplication/file-length guards clean: `guard-limits` and
      `format-check` (debug preset) exit 0; `tidy` (release preset, now per-file and
      incremental) exits 0 with zero errors across every new file including the
      fuzz target; `jscpd@4.0.5 --min-lines 8 --threshold 0 src include tools` reports
      0 clones.
- [x] `CHANGELOG.md` updated under `[Unreleased]/Added`.
- [x] Story-level self-audit checklist completed (below).
- [x] Epic row linked (`docs/backlog/epic-m1-vertical-slice.md`).

## Known issues

1. **The `kMaxDisambiguationAttempts` overflow-fallback format is this story's own
   design choice, not dictated by ADR-0010.** The ADR specifies "disambiguate
   deterministically (suffixing)" but does not pin an exact bound or an exact fallback
   shape once a bound is hit. This story picks the simplest deterministic option: try
   exactly `kMaxDisambiguationAttempts` numbered candidates (`(2)` through
   `(kMaxDisambiguationAttempts + 1)`), then fall back unconditionally to
   `desired + " (overflow)" + kMaxDisambiguationAttempts` — the bound value itself
   doubles as the fallback's counter, so the function needs no extra state to produce
   it. This fallback is **not** guaranteed collision-free against `taken` (it is never
   even checked against `taken`) — by design: once `kMaxDisambiguationAttempts`
   candidates are all taken, something is already very wrong (a real destination
   directory does not plausibly contain 10001 same-stem files), and the function's
   only remaining obligation is to terminate deterministically rather than loop
   forever. A future caller that needs a *guaranteed*-unique fallback (e.g. by mixing
   in a hash or GUID) can layer that on top; out of scope here (YAGNI — no story asks
   for it yet).
2. **`decodeUtf16Name` escapes a literal NUL code unit unconditionally**, even though
   `0x0000` is, strictly, a perfectly decodable BMP code point (it would map to a single
   raw NUL byte in UTF-8). This is a deliberate defense-in-depth choice, not a decoding
   limitation: `sanitizeOutputPath` (story-0060) already rejects any raw NUL byte in a
   name, so nothing downstream *requires* the decoder to special-case it — but the
   decoder now guarantees on its own, independent of the sanitizer, that a raw NUL byte
   can never appear in `DecodedName::utf8`. `lossless` correctly reports `false` for
   this case (the original code unit's literal value did not survive as a literal
   byte), matching the "escaped, not dropped" ADR-0010 policy for every other
   undecodable case.
3. **`NameDecodeFuzz.cpp`'s internal UTF-8 validator is intentionally not exhaustive.**
   It checks lead-byte shape and continuation-byte presence/well-formedness, but not
   overlong-encoding rejection or surrogate-code-point rejection (a UTF-8 sequence
   encoding U+D800-U+DFFF is technically ill-formed per the Unicode standard, but this
   validator would accept one). This is safe for this story's purpose: by construction,
   `decodeUtf16Name` never encodes a bare surrogate value through the normal code-point
   path (surrogates are always intercepted and escaped as ASCII `%uXXXX` first) and
   never produces an overlong encoding (the byte-length choice in `appendUtf8CodePoint`
   is a direct, minimal function of the code-point's value). A stricter validator would
   only ever agree with this one on every input `decodeUtf16Name` can actually produce;
   it was kept simple as a fuzz-target-local sanity net, not shipped as a general-purpose
   UTF-8 validator anyone else could come to depend on (YAGNI).
4. **Same "MSVC has no local `clang++`" gap as story-0060's `OutputPathFuzz.cpp`.**
   `NameDecodeFuzz.cpp` is wired into `tests/fuzz/CMakeLists.txt` the same way and is
   built/run by CI's Linux/Clang fuzz-smoke job; locally it is exercised only by
   `clang-tidy` (which does not require the `fuzz` preset's Clang toolchain to lint —
   it type-checks and analyzes the translation unit against the same compile database
   used for the library, since the file only depends on `revenant::fs::decodeUtf16Name`
   and standard headers).
5. Two genuine tidy-driven decomposition passes happened before this story's first
   green implementation was considered done (both fixed on this branch before any
   commit, same "tidy is the authority" instruction stories 0014/0060 already
   documented):
   - `NameDecodeFuzz.cpp`'s first draft wrote `continuationCount` as a straight
     if/else-if chain over four bit-mask comparisons; `readability-function-size`
     flagged it over the 10-statement threshold. Rewritten as a small `constexpr`
     table of `LeadShape{mask, value, continuationBytes}` entries walked by a single
     `for` loop — same logic, far fewer statements in the function body itself.
     `hasValidContinuations` was similarly split into `hasEnoughContinuationBytes` +
     `allContinuationBytesAreValid`.
   - `bugprone-easily-swappable-parameters` flagged `allContinuationBytesAreValid`'s
     adjacent `(std::size_t leadIndex, int count)` parameters. Suppressed with a
     justified `NOLINTNEXTLINE`, the same pattern already established at
     `ImageFileDevice.hpp`'s constructor and `InMemoryDeviceTest.cpp`'s
     `readThroughInterface`: a single, fixed-order call site (this function is only
     ever invoked from `hasValidContinuations`, itself only invoked from `isValidUtf8`'s
     own loop) makes the swap risk this check targets not apply here. One care point
     while applying it: `NOLINTNEXTLINE` only suppresses the *immediately following*
     line — a first attempt placed a multi-line justification comment directly above
     the function with the `NOLINTNEXTLINE` as the first of those lines, which
     suppressed nothing (it was "next line" relative to the *second comment line*, not
     the function). Fixed by putting `NOLINTNEXTLINE` as the last comment line,
     immediately before the function signature.
   - `src/fs/NameDecode.cpp`'s internal `DecodeContext{utf16le, unitCount}` struct was
     flagged by `cppcoreguidelines-pro-type-member-init` for not default-initializing
     `unitCount` in the class definition, even though every actual construction site
     designated-initializes both fields — fixed with `std::size_t unitCount = 0;`,
     matching the existing `JpegWalkOutcome`/`DecodedName` precedent of giving every
     non-class-type member a default value.
   - `modernize-use-designated-initializers` flagged the `kLeadShapes` table's
     positional-brace entries; switched to `{.mask = ..., .value = ..., .continuationBytes = ...}`
     per entry, matching the project's designated-initializer convention elsewhere.
   Every fix was verified not to weaken a test: the full 130/130 `ctest --preset debug`
   suite stayed green through every step, re-run after each batch of fixes.

## Story-level self-audit (docs/code-quality.md)

### Responsibility & clarity
- [x] Every new/changed function does exactly one thing at one abstraction level.
      `NameDecode.cpp`: `isHighSurrogate`/`isLowSurrogate`/`isDirectlyEscapable` (one
      predicate each); `unitAt` (read one LE code unit); `combineSurrogatePair` (one
      arithmetic combination); `appendContinuationByte`/`appendUtf8{One,Two,Three,Four}Byte`
      (one UTF-8 byte-length case each) + `appendUtf8CodePoint` (dispatch only);
      `hexDigit` (one nibble-to-char lookup); `appendEscapedCodeUnit`/`appendEscapedByte`
      (one escape format each); `appendCodePoint`/`appendEscapedUnit`/`appendTrailingByte`
      (one `DecodeState` mutation each); `decodeHighSurrogate` (resolve one high
      surrogate: pair or escape); `decodeStep` (classify-and-dispatch one code unit);
      `decodeFullUnits` (the accumulation loop); `decodeUtf16Name` (the four-step
      top-level pipeline: decode full units, handle a trailing odd byte, return).
      `Disambiguate.cpp`: `splitAtLastDot` (one split); `numberedCandidate` (one
      candidate string); `firstFreeNumberedCandidate` (the bounded search loop);
      `overflowFallback` (one fallback string); `disambiguate` (the three-step
      dispatch: free check, bounded search, fallback).
- [x] Each function's purpose is understood from its name and signature alone.
- [x] `NameDecode.{hpp,cpp}` (decode: UTF-16LE bytes → canonical UTF-8) and
      `Disambiguate.{hpp,cpp}` (suffix: a desired name + a "taken" oracle → a free
      name) are each focused on one responsibility, matching ADR-0010's own split
      between "decode" (filesystem layer, knows the encoding) and "disambiguate"
      (recovery/sink layer, knows what else is already written).

### Design
- [x] `decodeUtf16Name` has one reason to change: what "this UTF-16 content means in
      UTF-8" is. `disambiguate` has one reason to change: what "the next free name"
      means. Neither depends on the other.
- [x] No new type introduced needs DIP — both primitives are free functions over value
      types (`std::span`, `std::string`, `std::string_view`), matching `ByteReader`'s
      and story-0060's "leaf primitive" shape; `disambiguate`'s `taken` parameter is
      itself the DIP seam (a `std::function`, not a concrete directory-listing type) —
      the caller supplies whatever "is this name used" oracle fits its context.
- [x] YAGNI: no configurable escape format, no alternate surrogate policy, no
      pluggable disambiguation scheme (numeric suffix only, no timestamp/GUID
      variants) — only what ADR-0010 and this story's brief specify.
      `kMaxDisambiguationAttempts` is public for the same reason
      `kMaxSegmentBytes`/`kMaxSegments` are (story-0060): tests need to assert *at* the
      bound without re-hardcoding it.
- [x] No duplicated knowledge: the UTF-8 byte-length-by-value logic exists in exactly
      one place (`appendUtf8CodePoint`'s dispatch); the "where does the extension
      split" logic exists in exactly one place (`splitAtLastDot`), reused by both the
      free-name check (implicitly, via `desired` itself) and every numbered candidate.

### Anti-patterns
- [x] No God object — decoding one code unit, encoding one code point, and searching
      for a free name are each their own small function, not one function doing all of
      it.
- [x] No boolean-parameter traps — `decodeStep`/`decodeHighSurrogate`'s return value
      (units consumed: 1 or 2) is a meaningful count read at the call site, not a bare
      flag; `DecodedName::lossless` and `DecodeState::lossless` are named outcome
      fields, not control parameters.
- [x] No premature generality, dead code, or commented-out code.

### Correctness & safety
- [x] `decodeUtf16Name` is a total function over its input span — every branch
      (surrogate pair, unpaired/reversed surrogate, NUL, ordinary BMP unit, trailing
      odd byte, empty input) is handled explicitly; nothing throws, nothing is
      swallowed. `disambiguate` always terminates: the bounded loop plus the
      unconditional fallback cover every possible `taken` behavior, including one that
      never reports a name free (exercised directly by
      `Disambiguate.BeyondBoundReturnsOverflowFallback`).
- [x] Read-only / no I/O: neither function touches a file, a device, or the
      filesystem — `decodeUtf16Name` transforms an in-memory byte span,
      `disambiguate` only calls the caller-supplied `taken` predicate and builds
      strings.
- [x] No UB in byte handling: no `reinterpret_cast` anywhere in this story (the fuzz
      target's `toByteVector` uses the same per-element `std::ranges::transform` +
      `static_cast` idiom as every other fuzz target in this codebase); `unitAt` reads
      each 16-bit code unit through the existing bounds-respecting
      `fromLittleEndian<CodeUnit>` helper over a `subspan(...).first<2>()` — no
      unaligned dereference, no manual pointer arithmetic.

### Tests
- [x] Written test-first: RED captured (`LNK2019: unresolved external symbol ...
      decodeUtf16Name` / `... disambiguate`) with real interfaces and empty-body stub
      `.cpp` files wired into the build, before either function had an implementation;
      GREEN after the implementation (full transcripts in task-2-report.md).
- [x] Tests cover malformed/edge inputs by name: every surrogate shape (paired,
      unpaired-at-end, unpaired-followed-by-non-surrogate, reversed), both odd-byte
      shapes (trailing after valid content, and as the sole byte), the embedded-NUL
      case with an explicit no-raw-NUL assertion, and empty input, for
      `decodeUtf16Name`; the free/single-collision/multi-collision/extensionless
      shapes, and both bound-adjacent shapes (exactly-at-the-bound success, and
      never-free overflow) for `disambiguate`.
- [x] `decodeUtf16Name` — the byte-parsing entry point this story adds that consumes
      untrusted, attacker-influenced bytes straight off a filesystem — has a dedicated
      fuzz target (`NameDecodeFuzz.cpp`) asserting the "always valid UTF-8" invariant
      internally. `disambiguate` takes no untrusted bytes (only a caller-supplied
      predicate and a name already decoded/sanitized upstream) and so has no fuzz
      target of its own, matching the same scoping story-0060 used for
      `sanitizeOutputPath` vs. `boundedCount` (only the byte-parsing entry point gets
      fuzzed).
