<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0613: The layer DAG becomes a gate: an upward include is a build failure

- Epic: [epic-m6-loose-ends](../epic-m6-loose-ends.md)
- Status: Ready
- Size: S

## Goal

The architecture's central structural claim — "each layer depends only on the layer
below" — is enforced today by nobody reading carefully enough. An upward include entered
the tree in the fourth pull request this project ever merged and was still there twelve
pull requests later, having passed every gate on every one of them. Make the direction of
the layer DAG a mechanical verdict: a `tools/lint/check_layering.py` that fails the build
naming the file, the header, and the edge it violated.

## Design references

- [Architecture overview → layered design](../../architecture/overview.md) — line 24,
  "Each layer depends only on the layer below, through an interface", and the diagram
  under it. That diagram is the gate's specification; nothing else defines the order.
- [epic-m5 → milestone architecture audit](../epic-m5-performance.md#milestone-architecture-audit)
  — lines 134–143, the finding: "Nothing fired because nothing checks: the layer DAG is
  enforced by no gate — one static library, `src/` as a shared include root — and the
  inversion survived review and every PR since."
- [code-quality.md](../../code-quality.md) — line 96, "Are there recurring review findings
  that should become a new automated check?" This story is that question answered once.
- [story-0602](story-0602-python-duplication-gate.md) — the mold for a gate here: a Python
  script owning the rule and the verdict, fixture-tested so the *gate itself* is tested,
  with its rules chosen and justified rather than inherited. Also the rule this story
  obeys on ordering: "what the new gate finds on day one gets fixed or justified, never
  suppressed".
- [story-0607](story-0607-format-gate-argument-list.md) and
  [`tools/lint/source_set.py`](../../../tools/lint/source_set.py) — the one answer to
  "which files do the gates cover", plus the missing-root and empty-set refusals this
  gate inherits rather than reinvents.
- [story-0601](story-0601-safearith-neutral-home.md) and
  [story-0608](story-0608-namedecode-to-core.md) — the two cures that empty the violation
  list. This story is sequenced behind them; see the ordering decision below.
- [`src/CMakeLists.txt`](../../../src/CMakeLists.txt) — line 5, every layer in one static
  library; lines 191–194, `include/` public and the whole of `src/` private; line 218, the
  same root again for `revenant_cli`. Between them, the reason the compiler cannot object.
- [`cmake/DevTargets.cmake`](../../../cmake/DevTargets.cmake) — lines 107–114, the
  `guard-limits` target this gate joins.
- [`.github/workflows/ci.yml`](../../../.github/workflows/ci.yml) — the `guards` job (line
  26) and its file-length step (lines 41–42), where the CI leg goes.
- [`tests/unit/lint/test_check_format.py`](../../../tests/unit/lint/test_check_format.py)
  and `tests/CMakeLists.txt` lines 260–262 — the unit-test mold, and the `LintUnitTests`
  ctest entry that discovers `tests/unit/lint/` and needs no edit to pick up a new file.
- [quality-gates.md](../../testing/quality-gates.md) — lines 72–76: a gate change is a
  dedicated PR that updates [`AGENTS.md`](../../../AGENTS.md), that file, and the tool
  config together.

## What was measured

**The edge is older than the audit that found it.** `git log -S'fs/SafeArith.hpp' --
src/volume/` returns two commits: `5315704` (PR #5, the GPT reader — the inversion the M5
audit named) and, earlier, `5e29e3c` (PR #4, the MBR reader), which is where
`src/volume/MbrPlacement.cpp` first included `fs/SafeArith.hpp`. Twelve further pull
requests merged into `main` on top of it (#5 through #16), each running the full gate set
over that file. Nothing fired, on any of them.

**Nothing fires because nothing looks.** Verified, one mechanism at a time:

| Gate | What it looks at | Why an upward edge survives it |
|------|------------------|--------------------------------|
| the build | `src/CMakeLists.txt:5`, `:194`, `:218` | one static library holds every layer, and the whole `src/` root is an include path for all of it — any layer can include any other, in either direction |
| `clang-tidy` | `.clang-tidy:12` enables `misc-*` | `misc-header-include-cycle` finds *cycles*; `volume/ → fs/` is an inverted but perfectly acyclic edge, and `fs/` includes `volume/` **zero** times, so there is no cycle to find |
| `guard-limits` | `DevTargets.cmake:107-114` | counts lines |
| `format-check` | `DevTargets.cmake:26-41` | whitespace |
| CI `guards` | `ci.yml:26-55` | file length, formatting, duplication — no step reads an include line |

**Today's tree, after story-0601 landed.** Scanning all 325 `.cpp`/`.hpp` files under
`src/` and `include/revenant/` (branch `story/0601-safearith-neutral-home`, which moved
`SafeArith` to `core/` and removed two of the three upward edges):

| Edge | Count | | Edge | Count |
|------|------:|-|------|------:|
| `fs → core` | 214 | | `cli → recovery` | 21 |
| `carve → core` | 85 | | `recovery → carve` | 13 |
| `recovery → core` | 63 | | `cli → carve` | 7 |
| `volume → core` | 55 | | `cli → volume` | 3 |
| `cli → core` | 32 | | `recovery → volume` | 2 |
| `recovery → fs` | 28 | | `cli → fs` | 1 |
| | | | **`volume → fs`** | **1** |

525 cross-layer include edges. Exactly one points upward:
`src/volume/GptEntry.cpp:11`, `#include "revenant/fs/NameDecode.hpp"` — story-0608's, and
the last one left.

**The adjacency reading is already dead.** Of the 524 downward edges, only 89 land on the
immediately-lower layer; **435 skip at least one level** — every `fs → core` and
`carve → core` among them. Read literally, line 24 condemns most of the codebase.

**Two of the diagram's rungs are load-bearing only in one direction.** `fs → volume` and
`carve → fs` are both zero. The stack's order between those layers exists in the diagram
and in this gate, and nowhere else; the one place code has ever crossed the `volume`/`fs`
rung is the wrong way.

**The neighbors.** `tools/` includes `revenant/core/` (20) and `revenant/fs/` (7) and its
own `imagegen/` (114); `tests/` includes the public headers of five layers *and* the
private `src/`-relative headers of `cli/`, `carve/`, `fs/`, `volume/` and `recovery/`.
Nothing under `src/` or `include/` includes `imagegen/`, `support/`, `tools/` or `tests/`
— zero edges. And no file in `core/` outside `core/io/` includes `core/io/`.

## Design decisions

**The rule is direction, not adjacency.** A file in layer *L* may include any layer at or
below *L*; anything above is a violation. The allowed relation is the transitive closure
of

```
tools → cli → recovery → carve → fs → volume → core
```

taken from the overview's diagram top to bottom. Enforcing the prose literally would fail
435 existing edges whose only sin is that `fs/` reads `Result<T>` without asking `volume/`
first — a gate that has to be argued with on day one is a gate that gets switched off. The
DAG's real claim is that the arrows point one way, and that is what gets checked.

**The order is one list constant in the script, cited to the diagram.** Not a config file:
a second machine-readable copy of the layer stack is a second thing to drift. The
constant's comment names `docs/architecture/overview.md` and the diagram; changing one
without the other is a review finding, not something the gate can catch, and the story
says so rather than pretending otherwise.

**`core/io/` is folded into `core/`.** The diagram gives Device I/O its own rung, but both
live under one directory and the gate works at directory granularity. Measured cost of
folding: zero — no file in `core/` outside `io/` includes `core/io/` today. Splitting the
node means a sub-directory rule the tree has never needed. Same answer, same reason, for
`fs/ntfs` versus `fs/fat`: out of scope.

**`tools/` is a node above `cli/`; `tests/` is not walked at all.** `tools/imagegen` is a
consumer of the library exactly as the frontends are, and giving it the top node buys one
rule worth having: nothing in `src/` or `include/` may include the fixture builders
(`imagegen/` maps to the `tools` node), which is true today and would be a genuine
inversion if it stopped being. `tests/` gets no rule because a unit test's job is to reach
whatever it tests, private headers included — measured above, it reaches into five layers'
internals on purpose. Walking a tree where every edge is permitted is dead code.

**The roots are `guard-limits`' roots, and discovery is `source_set.py`'s.** `src`,
`include`, `tools` — the same three the file-length guard takes at
`DevTargets.cmake:110-111` and `ci.yml:42`. story-0607 made "which files do the gates
cover" have one answer; a third walker would make it two.

**An undeclared directory stops the gate.** A file under a walked root whose top-level
directory is not in the layer list exits 2 naming it, rather than being skipped. This is
the shard-index rule from `DevTargets.cmake:68-81` and the missing-root rule from
`source_set.py:24-25`, applied here: a new `src/` layer added without declaring it must
stop the build, because the alternative is a gate that silently checks less than it claims
while passing.

**It reads text, not the preprocessor.** Every line matching `#include` at line start,
after optional whitespace, is an edge — regardless of any `#if` around it. clang-format
guarantees that shape, a `//`-commented include does not match, and the conservatism runs
in the safe direction: the gate over-reports on conditional includes rather than under.

**The heavier alternative, considered and not taken.**

| Mechanism | What it would give | Fit |
|-----------|--------------------|-----|
| **Per-layer static link targets** — six libraries, each with its own `target_include_directories` naming only the layers beneath it | the *compiler* refuses an upward include; no script, no rule to keep in sync, no way to run the build without it | Not taken. It rewrites `src/CMakeLists.txt`'s "one shared static core" contract (lines 2–3) that both frontends and the test binary link, forces every new file into the right target or a link error explains itself badly, and narrows `include/` from one public root into six. That is an M-sized build-system change to enforce a rule that is 60 lines of Python — and the diagnostic it produces is a missing symbol, not "volume/ must not include fs/". Revisit if the layers ever ship separately; nothing plans to. |
| `clang-tidy misc-header-include-cycle` | already enabled | Catches cycles only. Measured above: it visited the offending file on thirteen pull requests and never fired. |
| `include-what-you-use` | unused and missing includes | A different question — hygiene per file, not direction between layers. |

**The gate lands after both cures. There is no allowlist.** story-0601 removed two edges;
story-0608 removes the third; this story goes green on arrival with no suppression
mechanism anywhere in it — no skip list, no baseline file, no "known violations" set to
grow into. The alternative — landing now with `GptEntry.cpp` in a checked-in shrinking
allowlist — was considered and rejected on this project's own precedent: story-0602 holds
that what a gate finds on day one is fixed or justified, never suppressed. A burn-down
list with an owner is defensible when the fix is far away; here the fix is a story in the
*same milestone* with an owner already assigned, so the list would be a worse copy of the
epic table, plus a file format, plus a rule that it must reach empty which nothing
enforces. If story-0608 slips out of M6, this story slips with it: a gate whose first act
is to except the one thing it was written to catch is a decoration.

**It joins `guard-limits` rather than adding a target.** That target is already "the
structural guards the compiler has no check for", and one more `COMMAND` keeps the local
pre-flight in [quality-gates.md](../../testing/quality-gates.md) at three lines instead of
four. One CI step, beside the file-length guard, in the same `guards` job.

## Acceptance criteria

- [ ] `tools/lint/check_layering.py` reports every include that crosses a layer boundary
      upward, naming the including file with its line number, the header it includes, and
      the edge in words — `volume/ must not include fs/`.
- [ ] It exits non-zero when at least one exists and zero when none does, and prints the
      cross-layer edge count alongside the verdict, so the shape of the DAG is visible and
      not only its legality.
- [ ] Both include spellings resolve to the same layer: `"revenant/fs/NameDecode.hpp"` and
      `"fs/NameDecode.hpp"` are one edge, `volume → fs`.
- [ ] File discovery is `source_set.py`; a root that does not exist and a file set that
      matches nothing both refuse to pass, as the format and coverage gates do.
- [ ] A file under a walked root whose top-level directory the layer list does not name
      stops the gate naming the directory.
- [ ] `cmake --build --preset debug --target guard-limits` runs it on Windows and Linux;
      the CI `guards` job runs it beside the file-length step.
- [ ] The tree passes at story close with no allowlist, baseline, skip list or suppression
      of any kind in the mechanism — story-0601 and story-0608 are merged first.
- [ ] Running the gate against the tree at `5315704` fails, naming all three historical
      edges: the gate demonstrably catches what thirteen pull requests did not.
- [ ] [quality-gates.md](../../testing/quality-gates.md)'s gate table and
      [AGENTS.md](../../../AGENTS.md) §6 name the new gate, per "Changing a gate"; the
      overview's layered-design section says which gate enforces its arrows.

## Test plan

Unit (`tests/unit/lint/test_check_layering.py`, discovered by the `LintUnitTests` ctest
entry with no CMake edit, over fixture trees built in a temp directory per
`test_check_format.py`'s `_touch` helper — a checked-in tree of deliberately wrong `.cpp`
files would be swept into the format and length gates' own roots):

- An upward include fails, and the message names the including file *and* line, the
  included header, and the violated edge — all three, because a verdict that names only
  the file sends the reader hunting.
- A downward skip-level include passes (`cli/` → `core/`), an adjacent one passes, and a
  same-layer include passes.
- Both spellings of the same header are one edge — the case that would otherwise have hid
  `GptEntry.cpp` behind its `revenant/` prefix.
- System and third-party includes (`<vector>`, `nlohmann/json.hpp`) are ignored.
- A `//`-commented-out include is not an include.
- A file under a directory the layer list does not name exits 2 naming the directory.
- An empty file set refuses to pass; a missing root refuses to pass.
- Multiple violations are all reported, not just the first — a gate that stops at one
  turns a cleanup into a queue.

End-to-end, one `add_test` in the `FormatGateRefusesAMissingRoot` mold
(`tests/CMakeLists.txt:165-169`): the real script, a missing root, `PASS_REGULAR_EXPRESSION`
on the refusal.

Recorded on completion, not automated: the run against `5315704` (acceptance criterion
above), and the run against the tree at close. Also not automated: that the layer list
still matches the diagram. That is read by a human, and the story says so instead of
implying a check exists.

## Definition of Done

- [ ] Acceptance criteria met, tests green under ASan + UBSan.
- [ ] clang-format, clang-tidy, duplication and file-length guard clean.
- [ ] `CHANGELOG.md` updated under `[Unreleased]`.
- [ ] Epic row linked.
- [ ] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
