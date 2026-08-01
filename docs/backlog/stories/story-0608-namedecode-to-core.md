<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0608: The UTF-16 name decoder moves down to `core/`, and `volume/` stops depending on `fs/`

- Epic: [epic-m6-loose-ends](../epic-m6-loose-ends.md)
- Status: Done
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
`volume/GptEntry.cpp:67` — the last being the only qualified one in production. There is
a second qualified call outside it, in `tests/fuzz/NameDecodeFuzz.cpp:45`; both drop the
`fs::`.

Two surprises, and both change the shape of the fix.

**Seventeen of the twenty-three consumers never call the function.** They include the
header for `DecodedName` — a `std::string` and a `bool` — or for `decodeRawName`. That
struct is the widest-travelled thing in the file and the least filesystem-shaped, so it
moves with the decoder. `misc-include-cleaner` (`misc-*`, warnings-as-errors) then decides
each consumer's include line by what it *names*, which splits them: eighteen name
`DecodedName` or `decodeUtf16Name` and so take the core header, while five name only
`decodeRawName` and keep `revenant/fs/NameDecode.hpp` alone — inheriting `DecodedName`
through it, which is correct, because they never write the type. Wide and one line deep.

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
`%uD834` and "not exact" is strictly better than one that says `?`.

**`passesThroughAsItself`'s address is caller-driven, and this story says so rather than
dressing it up.** The rule it holds — `/` may not split a path, the sigil may not make an
escape ambiguous — is about a *destination*, which by ADR-0010's own step 3 belongs in the
recovery/sink layer, and it needs no filesystem knowledge whatever. It stays in `fs/`
because both callers are there and no third one exists. That is an argument from call
sites, which is the same argument that kept `decodeUtf16Name` in `fs/` for four
milestones — so it is recorded as provisional, not settled: a caller outside `fs/` is what
should move it, and the ADR now says the same.

**What stays in `fs/`: the volume's own conventions, not the transform.**
`include/revenant/fs/NameDecode.hpp` keeps `decodeRawName` alone — ext4 enforces no
encoding, so its decoder validates and escapes rather than transcoding, which is a fact
about ext4 — and includes the core header for `DecodedName`. `decodeShortName` stays for
the same kind of reason, though not the one first written here: it holds no OEM code page
at all and deliberately refuses to guess one (`src/fs/fat/ShortName.hpp:22-27`); what it
does hold is FAT's `0xE5`/`0x05` deletion convention, the `NTRes` case bits and the 8.3
field widths. Choosing and running a decoder that needs those is filesystem knowledge;
turning code units into bytes is not.

**ADR-0010 is amended, not contradicted.** Its two-way split becomes three: *choosing* the
encoding — and running a decoder that needs the volume's conventions — stays in the
filesystem layer, *performing* the UTF-16LE-to-UTF-8 transform and spelling its lossless
escape move to `core/`, and sanitization stays in the recovery/sink layer. A Consequences
bullet records why — `volume/` decodes GPT labels with the same transform and reports the
same `lossless` flag — so the next reader finds the seam in the ADR rather than in a
`git log`.

**The split gives the escape sigil one home, which it needs precisely because of the
split.** `passesThroughAsItself` reserves `%` *because* the emitters spell an escape with
it; before this story both statements sat in one file, and afterwards they sit in two
directories with nothing binding them. So `%` becomes `kEscapeSigil` in
`src/core/NameEscape.hpp` — where it is emitted — and `src/fs/PathSafeByte.cpp` reads it
from there rather than repeating the literal. **The guarantee is structural, not tested:**
reverting this to two literals breaks nothing and fails nothing, because two literals
spelling `%` behave identically to one constant — what the constant buys is that they can
no longer be changed apart. The mutation below measures that they are genuinely wired
together, which is a different claim from "a test catches the revert", and this story does
not make the second one. It is the only place the story is not a pure move, and it repairs
a defect the move would otherwise have created.

**Zero behavior change.** No signature, no logic, no assertion changes. Both fs
namespaces nest inside `revenant`, so every unqualified call site resolves exactly as
before by enclosing-namespace lookup, and the two qualified ones — `volume/GptEntry.cpp`
and `tests/fuzz/NameDecodeFuzz.cpp` — drop their `fs::`. The diff is addresses, include
lines, two
`CMakeLists.txt` entries, four named constants standing where literals stood, and
`docs/install.md`'s manual fuzz-build recipe — whose file list turned out to be missing
six translation units and is completed here, because the story was editing that list
anyway and a recipe verified not to link is not something to leave behind. Anything
beyond that is scope creep and grounds to stop.

**Amendment, made at story close: the `docs` half of the fifth acceptance criterion is
narrowed to live references.** As written it asks for no `fs/NameEscape` anywhere under
`docs`; a grep returns six lines in two files, and all six stay. Five are in this story
file, describing the move it performs; the sixth is
[story-0302](story-0302-fat32-directory-entries.md):65, recording what story-0302 created
in M3. Rewriting either would falsify a record of what shipped, and story-0601 already
settled it the same way — story-0302 still names `src/fs/SafeArith.{hpp,cpp}` today,
after `c061d26` moved it, and that commit touched no closed story. So the criterion binds
code, comments in code, and documents that describe the tree as it *is*; a story file
describes the tree as it *was* on the day it was written. Recorded as an amendment rather
than edited into the criterion, because a criterion rewritten to fit its own outcome is
not evidence of anything.

**Not fixed here: a literal `%` in a UTF-16 name still passes through as itself**, which
makes it indistinguishable from an escape sigil — `fs/RawName.cpp` and
`fs/fat/ShortName.cpp` escape it, the UTF-16 path never has. It is pre-existing, it does
not change with the address, and folding it in would make a behavior change out of a
move.

## Acceptance criteria

- [x] `DecodedName` and `decodeUtf16Name` are declared in
      `include/revenant/core/Utf16Name.hpp` and defined in `src/core/Utf16Name.cpp`, in
      namespace `revenant`; `src/fs/NameDecode.cpp` is deleted, with nothing forwarding.
- [x] `appendEscapedByte` and `appendEscapedCodeUnit` live in
      `src/core/NameEscape.{hpp,cpp}` (namespace `revenant`); `passesThroughAsItself`
      lives in `src/fs/PathSafeByte.{hpp,cpp}` (namespace `revenant::fs`);
      `src/fs/NameEscape.{hpp,cpp}` is gone.
- [x] `include/revenant/fs/NameDecode.hpp` declares `decodeRawName` and nothing else.
- [x] A grep for `revenant/fs/NameDecode` under `src/volume` returns nothing, and a grep
      for `fs::decodeUtf16Name` over `src/` returns nothing: the upward edge is gone at
      both the include and the call. `src/volume/` now includes no `fs/` header at all,
      and `src/fs/` includes no `volume/` header — the rung is empty in both directions.
- [x] A grep for `fs/NameEscape` over `src include tests docs` returns nothing, comments
      and the `docs/install.md` fuzz recipe included — save one closed story's historical
      record and this story's own account of the move, per the amendment above.
- [x] All twenty-three consumers name the header that declares what they use; `tidy` is
      clean, which is `misc-include-cleaner` saying so. Evidence, clang-tidy 22.1.8: CI's
      four `tidy` shards over the whole tree, green on this branch's head — a fresh
      checkout, so no stamp can be stale; the same target run locally on Windows over 590
      translation units; plus a per-file run on Linux, against a clang compile
      database, over the fourteen translation units this diff touches that a default
      configure contains, over `tests/fuzz/NameDecodeFuzz.cpp` and
      `tests/fuzz/Ext4DirectoryEntryFuzz.cpp` against a `REVENANT_BUILD_FUZZERS=ON`
      database because a default one contains neither, and over the four remaining
      consumers this diff did *not* touch but newly left reaching `DecodedName` through
      `revenant/fs/NameDecode.hpp` — `src/fs/ext4/WalkCursor.cpp`,
      `tests/unit/fs/RawNameTest.cpp`, `tests/unit/fs/ext4/DirectoryEntryTest.cpp` and
      `tests/unit/fs/ext4/DirectoryHoleTest.cpp`.
- [x] The move is one commit, alone.

## Test plan

- `tests/unit/fs/NameDecodeTest.cpp` **moves whole** to `tests/unit/core/Utf16NameTest.cpp`
  — twelve tests, unedited but for the include, the `using`, and the suite label, which
  becomes `Utf16Name`: "moves whole" is a statement about test bodies, and a label naming
  a deleted header would send `--gtest_filter` hunting. It does not split into a core half
  and an fs half: all twelve call `decodeUtf16Name`, and the six that assert `%uD834` /
  `%AB` / `%u0000` are asserting the escape spelling, which moves too. An fs half would
  have nothing left to assert.
- The split the epic row imagines already exists, one directory over, and neither half's
  test bodies change: `tests/unit/fs/RawNameTest.cpp` is ext4's raw-byte policy including
  the `/` and `%` rules, and `tests/unit/fs/fat/ShortNameTest.cpp` is FAT's short-name
  policy — which is an escape policy, not a code page. Between them they are
  `passesThroughAsItself`'s coverage at its new address. `ShortNameTest` does change by
  two lines, its include and its `using`, because `DecodedName`'s address changed; that is
  the same one-line-deep edit the other seventeen core-header consumers take. `RawNameTest`
  changes not at all: it names only `decodeRawName`, whose header stayed put.
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
- **The one change that is not a move is measured by mutation, not by a regression test.**
  Set `kEscapeSigil` to `'!'` and nineteen tests fail, among them
  `RawName.APercentIsEscapedSoNoEscapeIsAmbiguous` and
  `FatShortName.ALiteralPercentIsEscapedSoAnEscapeStaysReadable` — the reservation side,
  in `fs/`, failing because a constant in `core/` moved. That is what proves the two
  halves are wired to one definition. It is *not* a test that catches the fix being
  reverted, and no such test exists or could: two literals spelling `%` behave exactly as
  one constant does. Run 2026-08-01 on the WSL bench; reverted immediately.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan (1057/1057 Windows,
      1041/1041 Linux).
- [x] clang-format, clang-tidy, duplication and file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
- [x] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed
      — three passes, all corrected. The first found one code defect: `%` left duplicated
      across the new boundary, fixed as `kEscapeSigil`. The other two found none, and
      between them twelve false statements *about* the code — the ADR resting on FAT's
      "OEM code page", which that decoder holds none of and says so; a comment the split
      disproved; a fuzz recipe rewritten while known not to link;
      `passesThroughAsItself`'s address argued as principle when it is caller-driven; the
      sigil's guarantee described as tested when it is structural; and seven counts,
      inventories and evidence claims that had drifted from the diff. The loop was stopped
      after the third pass on the rule in [code-quality.md](../../code-quality.md): two
      consecutive rounds found no code defect, and a 260-line story file for an S-sized
      move is itself the reason the remaining findings kept being prose.
- [x] One finding deferred, with an owner:
      [story-0613](story-0613-layer-dag-gate.md):89-90 reads present-tense about the
      `volume → fs` edge this story deletes, and :192-193 offers that edge as the gate's
      worked example. story-0613 is the next story in this milestone and its own
      fact-verification step owns the correction; nothing else in the tree depends on
      those two lines.
