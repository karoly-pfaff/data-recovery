<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0601: Move `fs/SafeArith.hpp` to a neutral home

- Epic: [epic-m6-loose-ends](../epic-m6-loose-ends.md)
- Status: Ready
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
- [`src/fs/SafeArith.hpp`](../../../src/fs/SafeArith.hpp) — whose own comment already
  says "shared by every on-disk geometry parser, filesystem and partition table alike".
  The header conceded the point in M4; only the address still disagrees.
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

And one surprise: a case-insensitive grep for all three names over `tests/` returns
**zero matches**. There is no `SafeArithTest` — the overflow paths are covered only
through their callers — so nothing in `tests/` moves, because nothing in `tests/`
knows these functions exist.

## Design decisions

**The home is `include/revenant/core/SafeArith.hpp` + `src/core/SafeArith.cpp`,
namespace `revenant` — not `revenant::core`.** No core header opens a nested `core`
namespace: `Result`, `Error`, `ByteReader`, `Endian` and `BoundedCount` all live in
plain `revenant`, and [AGENTS.md](../../../AGENTS.md) §1 wants namespaces short.
SafeArith goes on the shelf beside `BoundedCount`, which does the same job for counts
that SafeArith does for products and sums. The header comment's "not a public
interface" line goes with the old address: the shared header tree is exactly where a
utility with callers in four subtrees belongs.

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

- [ ] `safeMul32`, `safeMul64` and `safeAdd64` are declared in
      `include/revenant/core/SafeArith.hpp` and defined in `src/core/SafeArith.cpp`,
      in namespace `revenant`; `src/fs/SafeArith.hpp` and `src/fs/SafeArith.cpp` are
      deleted, with nothing forwarding from the old path.
- [ ] All seven consumer files include `revenant/core/SafeArith.hpp`; a grep for
      `fs/SafeArith` over `src include tests` finds nothing, comments included.
- [ ] A grep for `fs::safeMul` or `fs::safeAdd` over `src/` finds nothing: the four
      qualified call sites in `volume/` and the comment in `ext4/SuperblockFields.cpp`
      are updated.
- [ ] No test file is touched, and the full suite passes as-is — the mechanical proof
      that eighteen call sites still mean what they meant.
- [ ] The move is one commit, alone.

## Test plan

There is nothing to move in `tests/` — measured above, no test names these functions —
so the plan is correspondingly short.

- The entire existing suite, unmodified, green under ASan + UBSan. For a pure rename
  every existing test is a regression test; the overflow paths keep their indirect
  coverage through their callers, e.g. `RunlistExtentsTest`'s total-clusters-at-maximum
  case, which reaches `kOverflow` through `runlistExtents` and `safeMul64`.
- Not added: a dedicated `SafeArithTest`. A pure move adds no behavior, and the direct
  test these functions have never had is a coverage decision to make deliberately in a
  story of its own, not to smuggle into a rename.

## Definition of Done

- [ ] Acceptance criteria met, tests green under ASan + UBSan.
- [ ] clang-format, clang-tidy, duplication and file-length guard clean.
- [ ] `CHANGELOG.md` updated under `[Unreleased]`.
- [ ] Epic row linked.
- [ ] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
