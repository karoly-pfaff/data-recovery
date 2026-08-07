<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0704: A gate that inspected nothing fails — the vacuity refusal moves into `gate_files`

- Epic: [epic-m7-hardening](../epic-m7-hardening.md)
- Status: In review
- Size: S

## Goal

"An empty file set fails" is a convention five gate scripts each copy, and the one
enforcing [AGENTS.md](../../../AGENTS.md) §2's headline number does not have it. Make the
refusal a mechanism the next gate inherits instead of a habit it has to remember.

## Design references

- [`tools/lint/source_set.py`](../../../tools/lint/source_set.py) — `gate_files`, and a
  docstring that already names this gap: *"What is not owned here, and probably should be:
  the refusal to pass on an empty match … there are five copies of it now."*
- [`tools/lint/check_file_length.py`](../../../tools/lint/check_file_length.py) — imports
  `gate_files`, has no empty-set guard, and is the only script in `tools/lint/` with no
  unit test.
- [`tools/lint/check_coverage.py`](../../../tools/lint/check_coverage.py) — the fifth
  copy, and **structurally different**; see below.
- [`tools/lint/check_fuzz_instrumentation.py`](../../../tools/lint/check_fuzz_instrumentation.py)
  — answers the same question about an archive rather than a file set.
- [code-quality.md](../../code-quality.md) — the class this belongs to; M6 added four more
  instances of it.

## What is actually there

Seven `check_*.py` scripts. Six spell the empty-set refusal; one does not:

| Script | Walks files via `gate_files`? | Spells the refusal? |
|--------|:---:|:---:|
| `check_duplication.py` | yes | yes |
| `check_encoding.py` | yes | yes |
| `check_format.py` | yes | yes |
| `check_layering.py` | yes | yes |
| `check_file_length.py` | **yes** | **no** ← the §2 gate |
| `check_coverage.py` | no — reads a JSON export | yes, over `count == 0` |
| `check_fuzz_instrumentation.py` | no — reads an archive | yes, over a missing archive |

Six instances of the class in one milestone, plus five bespoke one-offs answering it
elsewhere. `check_file_length.py` is the one that matters most and the one that is missing.

## Design decisions

**The refusal moves into `gate_files`, which already owns the neighbouring question.**
A missing root is answered there — name it, exit 2 — and an empty match is the same kind
of answer to the same kind of caller. Moving it removes the four copies from the gates
that import it and, in the same motion, **gives `check_file_length.py` the guard it never
had**. That is the whole reason this is worth a story: the fix arrives at the one gate
nobody remembered, without anybody having to remember it.

**`check_coverage.py` keeps its own, and the story says so rather than pretending
otherwise.** It does not walk a tree — it reads a coverage export and refuses when no core
*line* was counted. That is the same principle over a different input, and folding it into
a file-set helper would either not fit or would make the helper about two things. One copy
of the rule survives this story by design; a story that claimed "the five copies go" would
be claiming a tidiness it did not deliver.

**The meta-test cannot be a naive glob, and the epic's sketch of it does not survive.**
The epic prescribes discovering "every `check_*.py` by glob" and asserting each "exits
non-zero over a root that exists and holds nothing". Two of the seven take no roots at all
— `check_coverage.py` wants `--export`, `check_fuzz_instrumentation.py` wants an archive —
so that test would fail them for the wrong reason (argument parsing), and a test that
passes for the wrong reason is the exact failure this milestone exists to remove.

What replaces it is one assertion with a real edge:

> Every `check_*.py` that walks a file set imports `gate_files`, and every gate that
> imports `gate_files` exits non-zero over a root that exists and holds nothing.

The first half is what catches the *seventh* gate: a new script that discovers files by
its own `rglob` fails the test, and the only way to pass is to use the helper — which
carries the refusal. The set of file-walking gates is derived by inspecting imports, not
hand-listed, so it cannot silently fall behind.

**A hand-maintained exemption list is refused.** The two non-walking scripts are
identified by "does not import `gate_files`", not by name. A list of names is a thing that
rots, and the next reviewer cannot tell an intentional exemption from a forgotten one.

**Exit code stays 2, not 1.** A vacuous gate is a configuration fault, the same class as a
missing root, and the existing scripts already distinguish 2 (cannot run) from 1 (found a
violation). CI treats both as failure; the distinction is for the human reading the log.

## Acceptance criteria

- [x] `gate_files` returns `None` — after reporting which roots produced nothing — when
      the resolved file set is empty, and its callers exit non-zero.
- [x] `check_duplication.py`, `check_encoding.py`, `check_format.py` and
      `check_layering.py` no longer carry their own copy of the rule.
- [x] `check_file_length.py` refuses an empty file set.
      *(The "first unit test file" half of this criterion was delivered by
      [story-0703](story-0703-gates-measure-python.md), not here. Recorded rather than
      claimed: the two stories landed a day apart and the credit is 0703's.)*
- [x] `check_coverage.py` keeps its own refusal, and a comment says why it is not the
      helper's.
- [x] A meta-test asserts both halves: every file-walking `check_*.py` imports
      `gate_files`; every importer refuses an empty-but-existing root.
- [x] The meta-test derives the gate list by inspection.
      *(The second half as originally written — "with no hand-maintained list of names" —
      is not met and could not be. The gates are derived, by whether they reach
      `gate_files`; but a gate with a required flag needs that flag, so `EXTRA_ARGS` names
      one. It fails loudly rather than spuriously — a gate whose flag is missing exits
      non-zero from argparse without `empty gate` on stderr — which is why the table is
      acceptable and the claim was not.)*
- [x] The meta-test **fails** when a new gate that walks files with its own `rglob` is
      added — demonstrated with a throwaway script during development, and that
      demonstration recorded here.
- [x] `source_set.py`'s docstring no longer describes the gap as open.

## Test plan

Unit (`tests/unit/lint/`):

- `test_source_set.py` — `gate_files` over an existing-but-empty root returns `None` and
  reports; over a root with one matching file, returns it. The empty case is distinct from
  the missing-root case, and both are asserted, because collapsing them would lose the
  distinction the exit codes make.
- `test_check_file_length.py` — **new file**. The empty root exits 2; a file over `--max`
  exits 1; a file under it exits 0. (This file is also created by
  [story-0703](story-0703-gates-measure-python.md); whichever lands second rebases onto
  the other rather than duplicating it.)
- `test_gate_vacuity.py` — the meta-test, both halves.

The negative that proves the meta-test works: a temporary `check_bogus.py` that globs its
own files makes it fail. Written, run, observed failing, and deleted — not left in the
tree. Per [code-quality.md](../../code-quality.md), a check is unverified until you have
watched it fail.

## Verified on completion (2026-08-07)

**"The five copies go" did not survive implementation, and the count was wrong twice.**
Counted from the diff rather than from the scoping note: **four** copies existed among the
gates that resolve files through `gate_files` — `check_encoding` and `check_layering` in
`main()`, deleted outright, and `check_duplication` and `check_format` in `run_gate`. The
fifth, `check_coverage`'s, is over a different input entirely. An earlier version of this
paragraph said "three sat in `main()`"; the diff shows two, and a story about counting
things measured this one from memory. The two in `run_gate` — `check_duplication.run_gate` and `check_format.run_gate` — sit in *public
functions the unit tests hand a list to directly*. Deleting those would let `run_gate([])`
report `0 block(s)` and a clean formatting pass, reachable from the API even though no
caller in the tree does it. That is the same vacuity defect one level in.

So the rule is **stated once and called at two levels**: `source_set.refuse_empty_gate`
owns the knowledge, `gate_files` calls it at the discovery boundary, and those two
`run_gate`s call it again on their own argument. Fewer copies of the *statement* was the
goal; fewer *checks* would have been a regression.

**`check_coverage.py` keeps its own, and the code now says why.** It walks no tree — it
refuses when the coverage export counted no core *line*. Same principle, different input;
folding it into a file-set helper would make the helper about two things.

**The meta-test asserts the mechanism, not the current membership.** The epic's sketch — glob
`check_*.py` and run each over an empty root — cannot work: `check_coverage.py` wants
`--export` and `check_fuzz_instrumentation.py` an archive, so both would fail on argument
parsing and pass for the wrong reason. What is asserted instead is described below; both
halves carry their own vacuity guard, because a glob that matched nothing would agree with
every assertion in the file.

**The detector was wrong before it was right, and the second version is the point.** The
first matched the substrings `rglob(`, `.glob(` and `os.walk(` over raw source. It misses
`iterdir`, `scandir`, `listdir`, `glob.iglob` and `Path.walk`; it fires on those words
appearing in a docstring, which this repository's gates are full of; and — worst — in the
green state its body never executed, so a typo in any marker would have left it green
forever. An instrument that cannot be shown to fire is not evidence, which is this
milestone's whole subject appearing inside the test written to enforce it.

It is over the AST now: any *call* to a discovery function, in any module under
`tools/lint/` except `source_set` itself. `DetectorTest` pins the detector against
synthetic sources on every run, so its positive branch always executes.

**Watched failing, with the case the first version would have missed.** A throwaway
`tools/lint/check_bogus.py` using `os.scandir` — which contains none of the three original
substrings — fails the meta-test with *"check_bogus.py calls ['scandir'] without reaching
gate_files"*. And it behaves exactly as predicted of the seventh gate: over an empty root
it printed `bogus gate: 0 file(s) inspected` and exited **0**. Written, run, observed
failing, deleted.

**`EXTRA_ARGS` is a hand-written table and the story says so** rather than claiming the
driven set is "derived, never named". The *gates* are derived — by whether they reach
`gate_files` — but a gate with a required flag needs that flag. The mechanism does not leak:
a gate whose flag is missing from the table exits non-zero from argparse without `empty
gate` on stderr, so the assertion fails loudly instead of passing spuriously. The redundant
`--warn 200 --max 250` is gone; those are `check_file_length`'s own defaults.

**An existing test was relying on an empty set passing.**
`test_a_suffix_outside_the_contract_is_not_measured` wrote only a 4,000-line `.md` and
expected exit 0. With the refusal in place the gate correctly stops before it can ignore
anything, so the test now writes a real source file alongside. It is worth recording that
the defect had already reached the tests: a check written to prove the gate ignores `.md`
was passing because the gate inspected nothing at all.

**`check_file_length.py` gains the guard it never had** — the gate enforcing
[AGENTS.md](../../../AGENTS.md) §2's headline number, which would report a clean pass over
a root holding no source. It is pinned by `test_a_root_that_matched_nothing_is_refused`.

## Definition of Done

- [x] Acceptance criteria met, tests green (ASan + UBSan).
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
- [x] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
- [x] `docs/testing/quality-gates.md` states the vacuity rule once, as a property of the
      gate framework rather than of each gate.
