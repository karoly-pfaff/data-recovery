<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0602: The duplication gate moves to Python, and Node.js leaves

- Epic: [epic-m6-loose-ends](../epic-m6-loose-ends.md)
- Status: In progress
- Size: S

## Goal

Every gate script in this repository is Python — except one. The DRY detector is
`jscpd`, which drags in Node.js, npm, a lockfile and 110 packages for the sake of a
single check. Replace it with a Python gate over the same tree, and delete the
JavaScript toolchain entirely.

## Design references

- [`.github/workflows/ci.yml`](../../../.github/workflows/ci.yml) — `npm ci
  --ignore-scripts` followed by `npx --no-install jscpd`, the only Node in the build.
- [story-0008](story-0008-ci-supply-chain-hardening.md) — pinned the npm tools by
  lockfile. This story removes what that story had to harden.
- [`tools/lint/check_coverage.py`](../../../tools/lint/check_coverage.py) — the shape a
  gate takes here: a pure function over structured data, driven in `ctest` by
  checked-in fixtures, so the *gate itself* is tested.
- [code-quality.md](../../code-quality.md) — "DRY is about *knowledge*, not textual
  similarity". This story's threshold decision is that sentence made mechanical.

## What was measured

Both candidates were run over `src include tools` on 2026-07-29, against the current
tree:

| Tool | Result | Fit |
|------|--------|-----|
| `jscpd 4.0.5 --min-lines 8 --threshold 0` | `Found 0 clones`, exit 0 | the incumbent |
| `lizard 1.23.0 -Eduplicate` | 50 duplicate blocks, 3.37% duplicate rate, **exit 0** | the candidate |
| `copydetect 0.5.0` | pairwise similarity percentages, HTML report | wrong shape — a plagiarism detector, not a clone gate |
| PMD CPD | — | the real `jscpd` equivalent, but Java: a worse dependency than the one being removed |

`lizard` is pure Python, pip-installable, and parses C++ properly. The 50-vs-0
disagreement is not a defect in either tool: `lizard` hashes *unified* tokens, so it
also finds blocks that are structurally identical but renamed, which `jscpd` at eight
literal lines does not. Most of what it reports is in `tools/imagegen/` — the NTFS
builders (`AttributeBuilder`, `FileAttributes`, `MftRecordBuilder`) and the FAT/NTFS
boot-sector fixtures.

That broader class is the one worth catching. Duplicated knowledge wearing different
identifier names is exactly what a DRY rule is for, and it is what the current gate
lets through.

## Design decisions

**Drive `lizard` through its API, not its command line.** The CLI has no threshold
flag — `min_duplicate_tokens` is only reachable from Python, and the CLI leaves it at
zero, which is why the bare run reports everything it can see. The CLI also always
exits 0, and prints prose rather than structured output. None of that can be a merge
gate. The API is the right shape: `get_duplicates(min_duplicate_tokens=N)` yields
`CodeSnippet(start_line, end_line, file_name)` groups, and `duplicate_rate()` gives the
headline. So `tools/lint/check_duplication.py` owns the threshold, the reporting and
the verdict, and `lizard` is reduced to what it is good at — tokenizing C++.

**The threshold is chosen, not inherited.** `jscpd`'s "8 lines" does not convert into a
token count, and pretending it does would smuggle in an unexamined number. The story
picks a token threshold, states what it corresponds to on this codebase, and records
why — the same way the 85% coverage floor and the 10% regression threshold are stated
numbers rather than defaults.

**What the new gate finds on day one gets fixed or justified, never suppressed.** The
threshold is not to be tuned upward until the tree goes green; that would be choosing
the number to fit the answer. Either the reported duplication is real knowledge
duplication and is removed, or it is coincidental structure — two builders that look
alike because the *formats* look alike, and which change for different reasons — and
the story says so explicitly per site. A gate calibrated by lowering the bar is a
decoration.

**Node leaves completely.** Not "unused but still committed": `package.json`,
`package-lock.json`, the `node_modules/` ignore rule, the `npm ci` step, and the
Node.js row in `docs/install.md`'s tool table all go. A dependency kept "just in case"
is still a dependency every contributor installs and every audit covers.

**Ordering.** Early, and for two reasons. The replacement has to be verified on both
platforms by CI rather than by the developer's word for it, so it wants to be among the
first things through the pipeline; and every later story that provisions a machine — the
Linux workbench M5 provisioned — then never needs Node on it at all.

## Acceptance criteria

- [ ] `tools/lint/check_duplication.py` reports duplicate blocks over given
      directories at a stated token threshold, naming each file and line range.
- [ ] It exits non-zero when a block at or above the threshold exists, and zero when
      none does.
- [ ] It reports the duplicate rate alongside the verdict, so a trend is visible and
      not only a pass/fail.
- [ ] The chosen threshold is documented with its rationale in
      [quality-gates.md](../../testing/quality-gates.md).
- [ ] Every block the gate reports on the current tree is either removed or recorded
      in this story as coincidental, with the reason.
- [ ] `ci.yml` runs the Python gate; the `npm ci` and `npx jscpd` steps are gone.
- [ ] `package.json`, `package-lock.json` and the `node_modules/` ignore rule are
      deleted; no Node.js is required to build, test, or gate this repository.
- [ ] `docs/install.md` and `docs/testing/quality-gates.md` describe the new gate;
      the Node.js row is gone from the tool table.

## Test plan

Unit (`tests/unit/lint/`, mirroring the coverage gate's fixture tests, driven from
`tests/CMakeLists.txt`): the verdict function over checked-in fixture sources — a pair
of files with an identical block above the threshold fails and names both sites; the
same pair below the threshold passes; a tree with no duplication passes; a block
duplicated three times is reported once with three sites, not three times.

Manual, recorded on completion: the gate's output on the tree at the chosen threshold,
and the disposition of every site it names.

Not automated: that `lizard` and `jscpd` agree. They do not, deliberately — the
comparison above is the record of that, not a test.

## Verified on completion (2026-07-31)

**Two claims in "Design decisions" above did not survive contact with the tool.**
`lizard`'s CLI does not leave the threshold at zero — it calls `get_duplicates()`
with the library default of 70, which is why the bare run reported 50 blocks and
not the 228 a zero would give. And the CLI's silence is worse than "no threshold
flag": `-T/--Threshold` exists but names the *function* metrics, and the exit code
is computed from those alone, so duplicates cannot affect it. Both point the same
way — the API, not the command line — which is why the decision stands.

**The threshold, and why it is not the one the API suggests.** `lizard`'s
`min_duplicate_tokens` is a bar on the tokens of every copy *added together*:
`sites × per-copy ≥ 2 × N`. Measured on this tree, that inverts what a DRY gate
wants. At `N = 150` it reported three blocks — a run of layout constants, the six
`FormatCarver` headers, and a twelve-site include preamble — while missing every
real clone, because a two-copy 133-token duplication weighs less than twelve
copies of 32. So the gate requires each copy on its own to reach the bar.

Even per copy, the reports were dominated by preambles: `lizard` unifies keywords
as well as identifiers and collapses literals, so `constexpr std::uint64_t kAOffset
= 0x00;` and `inline constexpr std::size_t kB = 0x14;` hash the same, and every
byte parser here opens with an include list, a namespace and an offset table.
Those are unfixable by construction — a gate reporting them is red for good. Hence
the second rule: a block every site of which lies outside a function body is not
reported. It cut the tree's blocks at 60 tokens from 30 to 9, and every one of the
21 it removed was a preamble, a constant table or a class declaration.

`-Ecpre`, which drops preprocessor lines, was measured as an alternative and
rejected: it removes 4 of the 18 blocks at 70 tokens but makes the bodies of the
three `#else` branches in `src/carve/` invisible to the gate. A cheaper gate that
checks less than it claims is the failure story-0607 and story-0612 exist for.

**Sixty tokens.** The median function in this tree is 62 tokens, over all 1478 of
them, so a block at the bar is a whole typical function's worth of code in two
places. Rounded down rather than up: 60 is stricter than the measurement, which
is the direction that cannot be an accommodation. The number and both rules are
documented in [quality-gates.md](../../testing/quality-gates.md), which owns them.

**What it found, and what happened to each.** Nine blocks, and all nine were
removed. None was justified as coincidental, and the threshold was not moved:

| Block | Sites | Disposition |
|-------|:-----:|-------------|
| 133, 78 tok | `JpegCarver.cpp` / `PngCarver.cpp` | Real. "Do the head bytes equal this signature" was written twice at length here and twice more, compactly, in `PdfCarver` and `ZipCarver`. Now `carve::headMatches` in `formats/HeadMatch.hpp`, called by all four. |
| 88 tok | `JpegCarver.cpp` / `PngCarver.cpp` | Same family; gone with it. |
| 86 tok | `MftRecordAttributes.cpp` ×2 | Real. Reading an attribute's content, parsing it and keeping what parsed is one protocol with two hooks; `consumeContent` holds it, and the two consumers state only their parser and their destination. |
| 79 tok | `exfat/PendingSet.cpp` / `fat/EntryFromSlot.cpp` | Real, and byte-identical. Following a chain to extents is `fs::extentsFollowingChain`; the contiguous case beside it (below the threshold, same knowledge) is `fs::extentsAssumingContiguous`. Both live with `chainExtents`, in `fs/ClusterChain.hpp`. |
| 71 tok | `ByteWriter.hpp` ×2 | Real. `putLe` and `putBe` each carried their own copy of the checked store loop that `putBytes` in the same header already was. Both now delegate to it. |
| 67 tok | `Endian.hpp` ×2 | Real. The four conversions ask one question — does the stored order differ from the native one — now asked once, in `detail::crossed`. The public signatures are unchanged. |
| 66, 61 tok | `exfat/BootRegion.cpp` / `fat/BootSector.cpp` | Mostly preamble rhyme, but it named a real fault: `a boot sector is 512 bytes` was stated in **four** files. It is now `fs::kBootSectorBytes` in `fs/MountRegion.hpp`, which already owns what every mounter reads. |
| 61 tok | `exfat/BootRegion.cpp` ×2 | Real. `withFatPlacement` and `withHeap` were the same read at different addresses; `withPair` takes the offsets and where the values land. |

The tree is green at 60 tokens per copy: `0 block(s)`, duplicate rate 3.60% (from
4.59% before the fixes). All 1010 tests pass under ASan + UBSan.

**Not claimed.** The gate scans `src include tools`, as jscpd did — the
[epic's note](../epic-m6-loose-ends.md#stories-added-by-the-m5-architecture-audit)
on `ArbitratedRecoveryTest.cpp` still stands, and no measurement here covers
`tests/`. "Every gate script is Python" is checked against the `guards` job and
`DevTargets.cmake`, not against the whole repository.

## Definition of Done

- [ ] Acceptance criteria met, tests green under ASan + UBSan.
- [ ] clang-format, clang-tidy, the new duplication gate, and the file-length guard
      clean.
- [ ] `CHANGELOG.md` updated under `[Unreleased]`.
- [ ] Epic row linked.
- [ ] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
