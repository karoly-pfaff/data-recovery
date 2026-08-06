<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0703: The gates measure the Python in `tools/`, and the 763-line seed generator is split

- Epic: [epic-m7-hardening](../epic-m7-hardening.md)
- Status: In review
- Size: M

## Goal

`tools/` is handed to the file-length and duplication gates as a root, and their shared
file discovery admits only `.cpp` and `.hpp`. So both gates walk a directory holding
3,796 lines of Python and report green over none of it. Make them measure what they are
handed.

## Design references

- [`tools/lint/source_set.py`](../../../tools/lint/source_set.py) — `SOURCE_SUFFIXES =
  {".cpp", ".hpp"}`, and `gate_files`, imported by all five walking gates.
- [`cmake/DevTargets.cmake`](../../../cmake/DevTargets.cmake) — `guard-limits` and
  `duplication` both pass `src include tools`.
- [`.github/workflows/ci.yml`](../../../.github/workflows/ci.yml) — the same three roots
  on the CI side.
- [story-0602](story-0602-python-duplication-gate.md) — **the discipline this story
  inherits**: a threshold is chosen from a measurement on this tree and stated with its
  rationale, never converted from another language's number; and what the gate finds on
  day one is fixed or justified per site, never suppressed by moving the bar.
- [`tools/fuzz/make_seed_corpus.py`](../../../tools/fuzz/make_seed_corpus.py) — 763 lines,
  three times the hard fail, the largest source file in the tree.
- [AGENTS.md](../../../AGENTS.md) §1 (the naming contract the suffix set encodes) and §2
  (the file-length limit, and the rule that widening scope is a decision).

## What is actually there

M6 moved 2,115 lines of Python into the blind spot — 13 files / 1,681 lines at `v0.3.1`
to **28 files / 3,796 lines** at HEAD. Both numbers verified at HEAD on 2026-08-06.

The exclusion is deliberate and unit-tested (`tests/unit/lint/test_source_set.py` asserts
a `.py` file is *not* discovered), so widening it is an [AGENTS.md](../../../AGENTS.md) §2
scope decision rather than a bug fix, and this story carries that amendment.

## Measured before scoping (2026-08-06, current tree)

Three numbers, taken so the story is scoped from the tree rather than from fear. **The
implementation re-measures rather than trusting these** — they are a week's drift from
being wrong, and the median moves with every story.

| Question | Method | Answer |
|----------|--------|--------|
| Python files over the 250-line ceiling | `wc -l` over `tools/**/*.py` | **one**: `make_seed_corpus.py` at 763 |
| Median Python function size | `lizard` over `tools/**/*.py`, 197 functions | **63 tokens** (C++: 62) |
| Duplicate blocks at 60 tokens/copy | the gate's own `duplicate_blocks` | **one block, 67 tok**, both sites *inside* `make_seed_corpus.py` |

**The two halves of this story converge on one file**, which is why it is an M and not an
L. The single duplicate block is `make_seed_corpus.py:139-144` against `:216-221`; the
split is likely to remove it as a side effect, and if it does not, it is one extraction.

## Design decisions

**Suffixes become a per-gate argument, not a wider global set.** This is the load-bearing
decision, and the obvious fix is wrong. Five gates share `gate_files`, and two of them are
inherently C++-only: `check_format.py` runs **clang-format**, and `check_layering.py`
enforces the **C++ include DAG**. Widening `SOURCE_SUFFIXES` in place would hand Python to
both — clang-format would reformat or reject it, and the layer gate would parse `import`
statements as includes. So `source_files`/`gate_files` take the suffix set they are to
walk, each gate states its own, and `SOURCE_SUFFIXES` stays the C++ answer under a name
that says so.

Verified rather than assumed: all five consumers were read before this was written, and
the two C++-only ones are why the story does not take the one-line change.

**Which gate gets Python, and why each:**

| Gate | Python? | Reason |
|------|:-------:|--------|
| `check_file_length.py` | **yes** | §2's limit is about responsibilities, and that is language-independent. |
| `check_duplication.py` | **yes** | `lizard` tokenizes Python properly; duplicated knowledge is the target. |
| `check_encoding.py` | **yes** | a stray cp1252 byte is a defect in any text file. |
| `check_format.py` | no | clang-format does not format Python. A Python formatter is a *new* gate and a separate decision. |
| `check_layering.py` | no | the layer DAG is a statement about C++ includes. |

**The duplication threshold for Python is chosen from a measurement.** 60 tokens is the
C++ number and it must not be inherited on the strength of the medians being one token
apart — that would be exactly the "converted rather than chosen" move
[story-0602](story-0602-python-duplication-gate.md) refused. The measurement above is the
input; the story states the number it picks and what that number corresponds to on this
tree. It may well land on 60. Landing there *by measurement* and by coincidence are
different facts, and only one of them survives the next language being added.

Whether story-0602's second rule — a block counts only when **every** site reaches a
function body — transfers to Python is an open question the implementation settles by
measurement, not by assumption. Python has no header preamble, which was that rule's whole
motivation, so it may be unnecessary; a module-level constant table would be the analogous
case. Whichever way it goes, the answer is recorded with the number.

**Splitting the seed generator is the consequence, not the goal.** `make_seed_corpus.py`
is split by responsibility — the same rule any 763-line C++ file would face — and the
split is driven by what the file does, not by getting under 250. Its output is covered by
`tests/unit/lint/test_seed_corpus.py`, which asserts it reproduces all 59 tracked seeds;
**that test passing unchanged, byte for byte, is what makes the split safe**, and it is
the acceptance criterion that matters most here.

**Not in scope: `tests/`.** The duplication gate has never covered `tests/`, the
[epic-m6 note](../epic-m6-loose-ends.md#stories-added-by-the-m5-architecture-audit) on
`ArbitratedRecoveryTest.cpp` still stands, and this story does not quietly change the root
list. Widening the *roots* is a different decision from widening the *suffixes*, and
mixing them would hide one behind the other.

## Acceptance criteria

- [x] `source_files`/`gate_files` take the suffix set to walk; no gate walks suffixes it
      cannot analyse.
- [x] `check_file_length.py`, `check_duplication.py` and `check_encoding.py` cover `.py`;
      `check_format.py` and `check_layering.py` demonstrably do not.
- [x] The file-length gate fails on the tree as it stands today, and passes once the split
      lands — demonstrated in that order, so the gate is proven to have teeth.
- [x] No file under `tools/` exceeds 250 lines.
- [x] The Python duplication threshold is stated in
      [quality-gates.md](../../testing/quality-gates.md) with the measurement it came from
      and the date of that measurement.
- [x] Every duplicate block the gate reports on the current tree is removed or recorded
      here as coincidental, with the reason. The threshold is not moved to make it green.
- [x] `tests/unit/lint/test_seed_corpus.py` passes unchanged, and the 59 generated seeds
      are byte-identical to those on `main` before the split.
- [x] `docs/testing/quality-gates.md` and [AGENTS.md](../../../AGENTS.md) §2 agree on what
      the file-length limit applies to.

## Test plan

Unit (`tests/unit/lint/`):

- `test_source_set.py` — the existing assertion that `.py` is excluded is **replaced**,
  not deleted: discovery with the C++ suffix set still excludes it, discovery with the
  Python set includes it. A test that simply disappears leaves no record that the
  behaviour was chosen.
- `test_check_file_length.py` — **new**, and the file gets its first unit test here.
  A `.py` file over the max fails; under it passes; the suffix set is honoured.
  (The vacuity guard this file also lacks belongs to
  [story-0704](story-0704-vacuity-refusal-in-gate-files.md); the two stories touch the
  same file and whichever lands second rebases.)
- `test_check_duplication.py` — a Python fixture pair above the chosen threshold fails and
  names both sites; below it, passes.
- `test_seed_corpus.py` — unchanged, and that is the point.

Measured and recorded on completion, not automated: the gate's output over `tools/` at the
chosen threshold, and the disposition of every block it names.

## Verified on completion (2026-08-06)

**Re-measured rather than trusting the scoping numbers**, as the story required. All three
held: 28 Python files / 3,796 lines under `tools/`; median function **63 tokens** over 197
functions; one file over the 250-line ceiling (`make_seed_corpus.py`, 763) and one over the
200-line warning (`loopdev/identity.py`, 205 — a warning, untouched).

**The threshold is 60, chosen from the Python measurement.** Story-0602 rounded a 62-token
C++ median down to 60; the same discipline over a 63-token Python median gives the same
number. **That the two agree is a coincidence of this tree, not an inheritance** — had the
Python median come out near 40 the gate would carry two numbers. Recorded that way in
[quality-gates.md](../../testing/quality-gates.md) so the next language is measured rather
than assumed.

**Story-0602's second rule does *not* transfer, and the first attempt to settle it
tested the wrong case.** That rule — a block counts only where every site reaches a
function body — exists in C++ because an include list and a table of on-disk offsets are
the only shape the language has for stating them. The story asked the right question while
scoping ("a module-level constant table would be the analogous case") and then answered it
with a fixture whose duplicated block sits *inside a function body in both files*, which
cannot test the rule it was claimed to settle. The self-audit built the case the story had
named: two modules sharing a constant table, **153 tokens per copy, reported as 0 blocks**
— `lizard`'s function list for a Python module holds no range covering module scope, so
the C++ rule discarded it silently while the documentation said gate 4 covered Python.

The rule is now C++-only. `tests/fixtures/duplication/python-module-scope/` is the case
that settles it, `tests/fixtures/duplication/mixed/` still pins the C++ half, and both
directions were watched: with the rule applied to Python the block is dropped, with it
C++-only the block is reported, and the C++ fixture is unaffected either way.

**The gate was demonstrated to have teeth before the split, in that order.** Over the tree
as it stood it failed with `ERROR tools/fuzz/make_seed_corpus.py: 763 lines (max 250)`;
after the split it passes. The same sequence is what `test_check_file_length.py` now holds
permanently — that file had no unit test at all, the only script in `tools/lint/` without
one.

**The split is by responsibility: one module per format family**, plus the driver. The
constraint that shaped it is not line count — it is that
`tests/unit/lint/test_seed_corpus.py` drives the generator by replacing
`make_seed_corpus.write`. **Every `write` call therefore had to stay in the driver**: a call
issued from another module would bind to that module's own imported `write` and silently
bypass the test's hook, which is a seed that stops being checked. So the builders moved and
the corpus manifest did not.

| Module | Lines | Holds |
|---|---:|---|
| `make_seed_corpus.py` | 150 | the corpus manifest — which seed goes where, and every `write` |
| `seed_ntfs.py` | 199 | MFT records, attributes, the `$MFT` region |
| `seed_ext4.py` | 190 | superblock, inode, extent tree, directory, journal, volume |
| `seed_partitions.py` | 119 | MBR and GPT disks |
| `seed_carve.py` | 104 | the six carve formats and five machinery inputs |
| `seed_boot_sectors.py` | 74 | NTFS, FAT32, exFAT boot sectors |
| `seed_fat.py` | 36 | FAT short and long-name entries |
| `seed_primitives.py` | 13 | `put` — the only name more than one builder uses |

All five tests in `test_seed_corpus.py` pass **unchanged**, so the 59 generated seeds are
byte-identical to those committed. That is the check that makes the split safe, and it was
run after the split and again after the duplication fix below.

**The one duplicate block the gate found, and what happened to it.** 67 tokens per copy,
both sites in what became `seed_ext4.py` — `ext4_inode` and `ext4_inode_record` each wrote
out the fixed inode header. Real duplicated knowledge: "how an ext4 inode's fixed fields are
laid out", stated twice. It is now `ext4_inode_head`, and the two builders state only what
differs — the block map they pass in and the tail `ext4_inode` adds. Not recorded as
coincidental, and the threshold was not moved. The tree is `0 block(s)` at 60 tokens per
copy over `src include tools`, duplicate rate 2.90%.

**A `__pycache__` guard was written and then removed.** Its justification — that a gate
walking `tools/` would otherwise lint whatever the interpreter cached there — is false:
`__pycache__` holds `.pyc`, which is in no suffix set, so no gate could ever have walked
one. The test written to prove the guard manufactured its own premise by creating a `.py`
*inside* `__pycache__`, which the interpreter never does. Both are gone. A guard against a
case that cannot occur is the same defect as a check that inspects nothing.

**Not done, and stated rather than hidden:** the roots are unchanged (`src include tools`),
so `tests/` is still outside the duplication gate — widening the *roots* is a different
decision from widening the *suffixes*, and mixing them would hide one behind the other. The
`epic-m6` note on `ArbitratedRecoveryTest.cpp` still stands.

**Three numbers in this section were wrong on the first pass, in a story whose thesis is
that measured numbers are load-bearing.** Three module line counts were stale, and the C++
median was quoted as 62 — story-0602's figure — while this tree measures **61** over 1,517
functions. Re-measuring Python and inheriting C++ is exactly the move the threshold
argument forbids; both are now measured and dated in
[quality-gates.md](../../testing/quality-gates.md), with a command that reproduces either.

**Also removed as dead weight:** the `SOURCE_SUFFIXES` alias, kept "so a gate that has not
been revisited still means C++" when all five gates were revisited in the same commit and
nothing referenced it; and the `suffixes` default, which contradicted this story's own rule
that a gate must state what it can analyse. There is no default now.

## Definition of Done

- [x] Acceptance criteria met, tests green (ASan + UBSan).
- [x] Coverage held or raised (≥ 85% core).
- [x] clang-format, clang-tidy, duplication, file-length guard clean — the last two now
      over Python as well.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
- [x] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
- [x] `docs/testing/quality-gates.md` records the Python threshold and its measurement.
