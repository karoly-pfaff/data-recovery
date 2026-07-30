<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0608: The UTF-16 name decoder moves down to `core/`, and `volume/` stops depending on `fs/`

- Epic: [epic-m6-loose-ends](../epic-m6-loose-ends.md)
- Status: Ready
- Size: S

## Goal

Turning UTF-16LE code units into UTF-8 is arithmetic over two-byte numbers. It knows
nothing about filesystems — but it lives at `revenant::fs::decodeUtf16Name`, because
NTFS was the first thing that needed it, and since M4 a GPT partition label has needed
it too. `src/volume/GptEntry.cpp:11` therefore includes `revenant/fs/NameDecode.hpp`
and line 67 spells `fs::` to call it: the layer DAG's only upward edge that is not
already [story-0601](story-0601-safearith-neutral-home.md)'s. Move the transform and
the lossless escape it emits into `core/`; leave in `fs/` the two things that really
are filesystem knowledge — which encoding a volume's names are in, and which bytes may
appear in a recovered path.

## Design references

- [epic-m6 → stories added by the M5 architecture audit](../epic-m6-loose-ends.md#stories-added-by-the-m5-architecture-audit)
  — this story's row, and the finding it retires.
- [epic-m5 → milestone architecture audit](../epic-m5-performance.md#milestone-architecture-audit)
  — "Did a layer leak? Yes — for the first time, and in the forbidden direction.
  `volume/` depends upward on `fs/` twice". This is the second of the two.
- [Architecture overview → layered design](../../architecture/overview.md) — "each
  layer depends only on the layer below", with `volume/` drawn one row *under* `fs/`.
- [ADR-0010](../../architecture/adr/adr-0010-filename-decoding-safe-output.md) — the
  sentence that put the decoder where it is: "Decoding lives in the filesystem layer
  (it knows the encoding); sanitization lives in the recovery/sink layer (it knows the
  destination). The two are separate responsibilities." Amended below.
- [story-0601](story-0601-safearith-neutral-home.md) — landed as `c061d26`, one commit
  back, and the shape this story copies: same audit, same cure, same milestone.
- [story-0302](story-0302-fat32-directory-entries.md) — which created *both* halves of
  this problem: `src/fs/SafeArith.{hpp,cpp}` and `src/fs/NameEscape.{hpp,cpp}`, each
  hoisted out of NTFS the moment FAT32 became a second caller. story-0601 moved the
  first one down to `core/` when the second caller turned out to be `volume/`. This is
  the same rule, applied to the second one.
- [story-0114](story-0114-filename-decoding.md) and
  [story-0404](story-0404-gpt-partition-table.md) — where `decodeUtf16Name` was written,
  and where `volume/` became its fourth caller.
- [AGENTS.md](../../../AGENTS.md) §1 — namespaces lowercase and short; file names
  `PascalCase`. Neither function name changes here.

## What was measured

Counted 2026-07-30 at the current tree. Includes of `revenant/fs/NameDecode.hpp` and
calls of `decodeUtf16Name`, per subtree:

| Subtree                  | Including files | `decodeUtf16Name` call sites |
|--------------------------|:---------------:|:----------------------------:|
| `include/revenant/fs/`   | 2               | 0                            |
| `src/fs/`                | 12 (+ `NameDecode.cpp` itself) | 3             |
| `src/volume/`            | 1               | 1                            |
| `tests/unit/`            | 6               | 12                           |
| `tests/fuzz/`            | 2               | 1                            |
| **Total**                | **23 consumers**| **17** (4 in production)     |

The four production call sites are `fs/ntfs/MftAttributes.cpp:108`,
`fs/exfat/PendingSet.cpp:29`, `fs/fat/LongNameAssembly.cpp:66` and
`volume/GptEntry.cpp:67` — the last being the only qualified one in the tree.

Two surprises, and both change the shape of the fix.

**Nineteen of the twenty-three consumers never call the function.** They include the
header for `DecodedName` — a `std::string` and a `bool` — or for `decodeRawName`. That
struct is the widest-travelled thing in the file and the least filesystem-shaped, so it
moves with the decoder, and `misc-include-cleaner` (`misc-*`, warnings-as-errors) means
every one of those nineteen files updates its include line rather than inheriting the
declaration transitively. Wide and one line deep.

**`decodeUtf16Name` contains no path policy at all.** Callers of `src/fs/NameEscape.hpp`,
counted:

| Function | Callers | Where |
|---|:-:|---|
| `appendEscapedCodeUnit` (`%uXXXX`) | 1 | `fs/NameDecode.cpp:111` — the mover, alone |
| `appendEscapedByte` (`%XX`)        | 3 | `fs/NameDecode.cpp:116`, `fs/RawName.cpp:96`, `fs/fat/ShortName.cpp:37` |
| `passesThroughAsItself` (`/`, `%`) | 2 | `fs/RawName.cpp:110`, `fs/fat/ShortName.cpp:36` |

The mover is not one of `passesThroughAsItself`'s callers. U+002F and U+0025 walk
straight out of `decodeUtf16Name` as `/` and `%`; the "would split a volume-relative
path" rule is asked only of bytes that arrived with no encoding promise. So the epic
row's "coupling GPT labels to ADR-0010's path-escaping policy" overstates it: what
actually couples a GPT label to `fs/` is the address, plus the *spelling* of the
escape. That is the seam, and it is a cleaner one than the row predicted.

## Design decisions

**Two files land in `core/`, and one splits.** `DecodedName` and `decodeUtf16Name` go
to `include/revenant/core/Utf16Name.hpp` + `src/core/Utf16Name.cpp`, namespace
`revenant` — not `revenant::core`, because no core header opens a nested namespace and
story-0601 just settled that (`Result`, `Endian`, `BoundedCount`, `SafeArith`). The
body moves verbatim: `src/fs/NameDecode.cpp` is deleted, not emptied. `NameEscape`
splits along the line the caller counts drew: the two emitters — the `%XX` / `%uXXXX`
spelling — become `src/core/NameEscape.{hpp,cpp}` in namespace `revenant`, which both
the moved decoder and the two remaining `fs/` decoders include downward;
`passesThroughAsItself` stays behind as `src/fs/PathSafeByte.{hpp,cpp}`, renamed
because one function is all that is left and "escape" is no longer what it does.

**The escape spelling is not filesystem policy, and belongs with the transform.**
`%uD834` is how the decoder stays *total* over hostile bytes — the alternatives are
U+FFFD or dropping, and a recovery tool that silently discards bytes has failed at its
one job. `GptEntry::nameIsExact` is `DecodedName::lossless`; a partition label that says
`%uD834` and "not exact" is strictly better than one that says `?`. The rule that *is*
about a destination — `/` may not split a path, `%` may not make an escape ambiguous —
stays in `fs/`, where its only two callers are.

**What stays in `fs/`: the encoding, not the decoding.**
`include/revenant/fs/NameDecode.hpp` keeps `decodeRawName` alone (ext4 enforces no
encoding — a fact about ext4) and includes the core header for `DecodedName`.
`fs/fat/ShortName.cpp` keeps its OEM code page. Choosing which decoder a volume's names
need is filesystem knowledge; running one is not.

**ADR-0010 is amended, not contradicted.** Its two-way split becomes three: *choosing*
the encoding stays in the filesystem layer, *performing* the UTF-16LE-to-UTF-8 transform
and spelling its lossless escape move to `core/`, and sanitization stays in the
recovery/sink layer. A Consequences bullet records why — `volume/` decodes GPT labels
with the same transform and reports the same `lossless` flag — so the next reader finds
the seam in the ADR rather than in a `git log`.

**Zero behavior change.** No signature, no logic, no assertion changes. Both fs
namespaces nest inside `revenant`, so all sixteen unqualified call sites resolve exactly
as before and `volume/` simply drops a `fs::`. The diff is addresses, include lines, two
`CMakeLists.txt` entries and one line of `docs/install.md`'s manual fuzz-build recipe,
which names `src/fs/NameDecode.cpp`. Anything more is scope creep and grounds to stop.

**Not fixed here: a literal `%` in a UTF-16 name still passes through as itself**, which
makes it indistinguishable from an escape sigil — `fs/RawName.cpp` and
`fs/fat/ShortName.cpp` escape it, the UTF-16 path never has. It is pre-existing, it does
not change with the address, and folding it in would make a behavior change out of a
move.

## Acceptance criteria

- [ ] `DecodedName` and `decodeUtf16Name` are declared in
      `include/revenant/core/Utf16Name.hpp` and defined in `src/core/Utf16Name.cpp`, in
      namespace `revenant`; `src/fs/NameDecode.cpp` is deleted, with nothing forwarding.
- [ ] `appendEscapedByte` and `appendEscapedCodeUnit` live in
      `src/core/NameEscape.{hpp,cpp}` (namespace `revenant`); `passesThroughAsItself`
      lives in `src/fs/PathSafeByte.{hpp,cpp}` (namespace `revenant::fs`);
      `src/fs/NameEscape.{hpp,cpp}` is gone.
- [ ] `include/revenant/fs/NameDecode.hpp` declares `decodeRawName` and nothing else.
- [ ] A grep for `revenant/fs/NameDecode` under `src/volume` returns nothing, and a grep
      for `fs::decodeUtf16Name` over `src/` returns nothing: the upward edge is gone at
      both the include and the call.
- [ ] A grep for `fs/NameEscape` over `src include tests docs` returns nothing, comments
      and the `docs/install.md` fuzz recipe included.
- [ ] All twenty-three consumers name the header that declares what they use; `tidy` is
      clean, which is `misc-include-cleaner` saying so.
- [ ] The move is one commit, alone.

## Test plan

- `tests/unit/fs/NameDecodeTest.cpp` **moves whole** to `tests/unit/core/Utf16NameTest.cpp`
  — twelve tests, unedited but for the include and the `using`. It does not split into a
  core half and an fs half: all twelve call `decodeUtf16Name`, and the six that assert
  `%uD834` / `%AB` / `%u0000` are asserting the escape spelling, which moves too. An fs
  half would have nothing left to assert.
- The split the epic row imagines already exists, one directory over, and both halves
  stay untouched: `tests/unit/fs/RawNameTest.cpp` is ext4's raw-byte policy including the
  `/` and `%` rules, and `tests/unit/fs/fat/ShortNameTest.cpp` is FAT's code page. Between
  them they are `passesThroughAsItself`'s coverage at its new address.
- `tests/fuzz/NameDecodeFuzz.cpp` stays whole and stays where it is, gaining one include.
  It deliberately drives both decoders on the same input — "a decoder is judged on what it
  does with bytes that were never meant for it" — and splitting it would halve the
  always-valid-UTF-8 invariant it exists to assert. A fuzz target may span two layers; it
  is not part of the DAG.
- The rest of the suite runs unmodified, green under ASan + UBSan. For a pure move every
  existing test is the regression test.
- Not added: a `PathSafeByteTest`. A move adds no behavior, and a first direct test for a
  predicate that has had indirect coverage since M3 is a deliberate coverage decision, not
  something to smuggle into a rename.

## Definition of Done

- [ ] Acceptance criteria met, tests green under ASan + UBSan.
- [ ] clang-format, clang-tidy, duplication and file-length guard clean.
- [ ] `CHANGELOG.md` updated under `[Unreleased]`.
- [ ] Epic row linked.
- [ ] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
