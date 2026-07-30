<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0601: Move `fs/SafeArith.hpp` to a neutral home

- Epic: [epic-m6-loose-ends](../epic-m6-loose-ends.md)
- Status: In review
- Size: S

## Goal

`safeMul32`, `safeMul64` and `safeAdd64` are overflow-checked arithmetic over untrusted
on-disk numbers — and nothing else. They were born beside their first caller and have
kept its address through two generations of new ones: `revenant::fs`, in a layer
diagram where `volume/` sits *below* `fs/` and has been calling upward across that
boundary since M4. Move the three functions to `core/`, where cross-cutting guards over
hostile numbers already live, and delete the old address.

## Design references

- [epic-m4](../epic-m4-devices-partitions.md) — the milestone audit's finding, verbatim:
  "`SafeArith` now has a caller outside `fs/` and should not keep that namespace."
- [`SafeArith.hpp`](../../../src/core/SafeArith.hpp) — whose own comment already said
  "shared by every on-disk geometry parser, filesystem and partition table alike" while
  it still lived under `src/fs/`. The header conceded the point in M4; only the address
  disagreed.
- [Architecture overview → layered design](../../architecture/overview.md) — "each
  layer depends only on the layer below", and `volume/` is below `fs/`. The namespace
  wart is also an upward edge.
- [story-0302](story-0302-fat32-directory-entries.md) — the first hoist: these
  functions left the NTFS boot sector for `src/fs/` when FAT32 became their second
  caller, "moved rather than copied". This story is the same rule, one layer further
  down.
- [story-0003](story-0003-result-and-byte-utilities.md) and
  [`include/revenant/core/BoundedCount.hpp`](../../../include/revenant/core/BoundedCount.hpp)
  — where the cross-cutting guards live and the namespace they use. `boundedCount` is
  the exact sibling: one checked operation over an untrusted on-disk number, returning
  `Result`.
- [AGENTS.md](../../../AGENTS.md) §1 — namespaces lowercase and short (`revenant`,
  `revenant::carve`); file names `PascalCase` per main type. The move changes neither
  the file name nor the function names.

## What was measured

Counted 2026-07-30 at the current tree — includes of `fs/SafeArith.hpp` and calls of
the three functions, per subtree:

| Subtree          | Including files | Call sites |
|------------------|:---------------:|:----------:|
| `src/fs/ntfs/`   | 3               | 4          |
| `src/fs/fat/`    | 1               | 6          |
| `src/fs/exfat/`  | 1               | 4          |
| `src/volume/`    | 2               | 4          |
| **Total**        | **7** (+ `SafeArith.cpp` itself) | **18** |

Only `volume/` spells the qualifier: the four `fs::safe*` call sites in
`MbrPlacement.cpp` and `GptPlacement.cpp` are the wart the audit named. The fourteen
call sites inside `fs/` are unqualified. Two comments also name the old address —
`src/fs/ext4/SuperblockFields.cpp:74` (`fs::safeMul32`) and
`src/fs/ntfs/BootSectorInternal.hpp:5` — and one build entry does,
`src/CMakeLists.txt` line 55. ext4 itself is not a caller; it borrows the
diagnostic-offset convention, not the functions, which is why the epic lists three
filesystems and not four.

And one surprise, measured *before* this story changed anything: a case-insensitive grep
for all three names over `tests/` returned **zero matches**. There was no `SafeArithTest`
— the overflow paths were reachable only through their callers — so nothing in `tests/`
had to move, because nothing in `tests/` knew these functions existed. What the story
concluded from that turned out to be wrong; see the test plan.

## Design decisions

**The home is `src/core/SafeArith.hpp` + `src/core/SafeArith.cpp`, namespace
`revenant` — not `revenant::core`.** No core header opens a nested `core` namespace:
`Result`, `Error`, `ByteReader`, `Endian` and `BoundedCount` all live in plain
`revenant`, and [AGENTS.md](../../../AGENTS.md) §1 wants namespaces short.

**It stays internal — `src/core/`, not the public `include/` tree.** The first draft
put it beside `BoundedCount` in `include/revenant/core/`, and the self-audit caught what
that costs: [`src/CMakeLists.txt`](../../../src/CMakeLists.txt) documents the rule in as
many words — internal headers shared across layer directories are included from the
source root and are *not* part of the public interface — and this header is exactly that
case. Nothing outside `src/` calls these functions, and no public header names them, so
publishing them would be YAGNI in its purest form: an interface with no consumer.
[versioning.md](../../versioning.md) sharpens rather than settles it — the `librevenant`
API is explicitly *not* covered by SemVer at 1.0, only "once explicitly declared stable
in its own ADR" — which means whatever sits in `include/` is what that future ADR has to
reason about. Keeping an internal helper out of it keeps that decision smaller; it does
not make the header a compatibility promise today. `BoundedCount.hpp` sits in the public
tree and is not referenced by any public header either — a pre-existing inconsistency
this story notes and does not widen. (`src/core/` also keeps it outside clang-tidy's
`HeaderFilterRegex`, exactly as `src/fs/` did; that gap belongs to every internal shared
header and is not this story's to close.)

**Inside `fs/`, only include lines change.** `revenant::fs` nests inside `revenant`,
so all fourteen unqualified call sites in `ntfs/`, `fat/` and `exfat/` resolve exactly
as before, spelled exactly as before. The whole diff is: eight include lines, four
dropped `fs::` qualifiers in `volume/`, two comment fixes, one `CMakeLists.txt` line,
and the namespace blocks of the header and its `.cpp`. A diff larger than that is
scope creep, and grounds to stop.

**The old path dies outright; no forwarding shim.** Pre-1.0, every caller is in this
tree, and every one updates in the same commit. A shim exists to keep out-of-tree
callers compiling; here it would only preserve the wart this story exists to remove.

**Zero behavior change.** No signature changes, no logic changes, no test changes.
`git diff` reads as addresses and qualifiers, nothing else — which is why this waited
for a quiet milestone instead of widening a feature story.

**This sets the precedent the NameDecode story follows.** The M5 audit found the same
shape one file over — `src/volume/GptEntry.cpp:11` includes `revenant/fs/NameDecode.hpp`,
an upward `volume/` → `fs/` edge — and that fix (the UTF-16 decoder moves to `core/`)
is a separate story, deliberately not folded into this one.

## Acceptance criteria

- [x] `safeMul32`, `safeMul64` and `safeAdd64` are declared in
      `src/core/SafeArith.hpp` and defined in `src/core/SafeArith.cpp`, in namespace
      `revenant`, and the header is not in the public include tree; `src/fs/SafeArith.hpp`
      and `src/fs/SafeArith.cpp` are deleted, with nothing forwarding from the old path.
- [x] All seven consumer files include `core/SafeArith.hpp`; a grep for
      `fs/SafeArith` over `src include tests` finds nothing, comments included.
- [x] A grep for `fs::safeMul` or `fs::safeAdd` over `src/` finds nothing: the four
      qualified call sites in `volume/` and the comment in `ext4/SuperblockFields.cpp`
      are updated.
- [x] No *existing* test file is touched, and the full suite passes as-is — the
      mechanical proof that eighteen call sites still mean what they meant. One test
      file is **added**; see the test plan for why the original "none needed" was wrong.
- [x] `safeMul32`, `safeMul64` and `safeAdd64` each have both branches covered by a
      direct test.
- [x] The move lands as one commit, alone — the branch carries the move, its self-audit
      rework and a formatting fix, and the squash-merge this repository uses
      ([git-workflow.md](../../git-workflow.md)) delivers them to `main` as one.

## Test plan

There is nothing to *move* in `tests/` — measured above, no test named these functions.
The first draft concluded from that "nothing to add either", and the self-audit
falsified the reasoning behind it:

- The entire existing suite, unmodified, green under ASan + UBSan. For a pure rename
  every existing test is a regression test.
- **`tests/unit/core/SafeArithTest.cpp` is added.** The draft justified adding no test
  by claiming the overflow paths keep indirect coverage through their callers. That is
  true of exactly one of the three functions: `RunlistExtentsTest`'s
  total-clusters-at-maximum case reaches `kOverflow` through `runlistExtents` and
  `safeMul64`, and **nothing in the tree reaches `safeMul32`'s or `safeAdd64`'s
  rejection branch at all** — before this story or after it. Deferring the direct test
  to "a story of its own" would have been a note-to-self for a story nobody filed, which
  [code-quality.md](../../code-quality.md) forbids by name. Eleven cases: each function's
  in-range result, its boundary, its rejection with `kOverflow` *and* the diagnostic
  offset intact, and the zero-operand guard that keeps `safeMul64`'s division defined.
  Modelled on `BoundedCountTest`, the sibling guard that already had one.
- **The boundary is pinned from the accept side, and that was proved by mutation.** The
  second self-audit round found `safeMul64`'s guard — `b > max / a` — unpinned:
  relaxing it to `>=` passed all 1008 tests, because every probe that existed, here and
  in `RunlistExtentsTest`, sat on the reject side. The largest-accepted-operand case
  closes it; with the guard mutated to `>=`, that case and only that case fails. A
  boundary test nobody has watched fail is a boundary test nobody should trust.

## Definition of Done

- [x] Acceptance criteria met, tests green under ASan + UBSan (1008/1008).
- [x] Coverage held or raised (≥ 85% core) — raised: two previously unreached rejection
      branches in `core/` now have direct tests.
- [x] clang-format, clang-tidy, duplication and file-length guard clean — clang-tidy
      re-run from cleared stamps, since the diff moves a header.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked. *(The epic's prose is rewritten to the past tense in the M6
      backlog-docs commit that also files story-0608 onward — deliberately not on this
      branch, which stays the move and nothing else.)*
- [x] Docs/ADRs updated if the design changed — no ADR: the layer assignment the move
      restores is the one [overview.md](../../architecture/overview.md) already states,
      and the header stays internal, so no published interface changed.
- [x] Story-level self-audit checklist ([code-quality.md](../../code-quality.md))
      completed — first round REWORK (six findings: the build entry left in the `fs/`
      block, a false justification for adding no test, unrelated work on the branch, the
      public-surface enlargement, a dangling comment fragment, and stale epic prose), all
      resolved; second round pending.
