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

## Definition of Done

- [ ] Acceptance criteria met, tests green under ASan + UBSan.
- [ ] clang-format, clang-tidy, the new duplication gate, and the file-length guard
      clean.
- [ ] `CHANGELOG.md` updated under `[Unreleased]`.
- [ ] Epic row linked.
- [ ] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
