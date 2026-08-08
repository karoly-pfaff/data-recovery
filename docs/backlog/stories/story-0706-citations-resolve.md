<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0706: A `path:line` citation that no longer resolves fails the build

- Epic: [epic-m7-hardening](../epic-m7-hardening.md)
- Status: Done
- Size: S

## Goal

Stale `path:line` references were fixed by hand four times inside M6 — the single most
repeated review finding of the increment, and one that burns adversarial audit rounds,
the most expensive resource this project spends. Make the build resolve them, and add the
one rule that stops the class regrowing.

## Design references

- [code-quality.md](../../code-quality.md) — the checklist that gains the citation rule.
- [`tools/lint/`](../../../tools/lint/) — where the script goes.
- [story-0704](story-0704-vacuity-refusal-in-gate-files.md) — this gate is subject to the
  vacuity rule like any other; a run that resolved no citations must fail.
- [story-0602](story-0602-python-duplication-gate.md) and
  [story-0610](story-0610-partition-scope-once.md) — between them they hold every
  unresolvable citation in the tree today.

## Re-measured before implementing (2026-08-08)

The numbers below were taken on 2026-08-06 and had already moved by the time the
gate was written — **by this milestone's own hand**. story-0702 shrank
`RecoveryOptions.cpp` from 216 lines to 100 when it collapsed the flag surface into
one table, which stranded three more citations past EOF; a fourth aged out of
`README.md`; and `README.md` acquired a second and third namesake, so a citation
naming it is now *ambiguous* rather than resolved.

| | Scoped (2026-08-06) | At implementation (2026-08-08) |
|---|:---:|:---:|
| Unresolved citations | 8 | **12** |
| …past EOF | 6 | **9** |
| …naming no file | 2 | 2 |
| …ambiguous | 0 | **1** |

The design survived; its measurements did not. That is the story's own thesis
arriving early: the class regrows, and it regrew *inside the milestone that was
writing the gate to stop it*. The zero in the ambiguity row was called out when
this was scoped as "a property of today's tree, not a law" — it stopped being
true in two days.

## Measured when scoped (2026-08-06)

The epic estimated "162 citations across 14 story files" and six past EOF. Re-measured
against the tree, the shape is right and two of the numbers are not:

| | Epic | Measured |
|---|---|---|
| Citations in `docs/**.md` | 162 | **172** total; 147 of them in story files |
| Docs holding them | 14 story files | **13 docs**, of which 12 are story files |
| Provably past EOF | six | **six** ✓ |
| Naming a file that does not exist | — | **two** |

The six past EOF — written here **without** citation syntax, for the reason in Design
decisions below:

| Doc | File cited | Lines cited | File is | Fails by |
|-----|------------|:-----------:|:-------:|----------|
| story-0602 | `JpegCarver.cpp` | 47–65 | 57 | 8 lines |
| story-0602 | `PngCarver.cpp` | 55–73 | 65 | 8 lines |
| story-0610 | `PartitionedWalk.cpp` | 95–101 | 95 | 6 lines |
| story-0610 | `PartitionedWalk.cpp` (full path) | 96 | 95 | 1 line |
| story-0610 | `PartitionedWalk.cpp` | 96 | 95 | 1 line |
| story-0610 | `PartitionedWalk.cpp` | 97–98 | 95 | 3 lines |

The two missing paths are both `NameDecode.cpp` cited under an `fs/` prefix in
[story-0608](story-0608-namedecode-to-core.md), whose file moved.

## The finding that shapes the design

**Most citations here are bare basenames, not repo-relative paths.** The story-0602 row
above cites `JpegCarver.cpp` and names no directory. A gate that resolved "the path as
written" would report ~110 of the
172 as missing files and be useless on day one — which is what a first pass at this
measurement did before the resolution rule was written properly.

So resolution has **three** outcomes, not two:

- **resolved** — the citation is a real path, or exactly one file in the tree bears that
  basename (or that path suffix, e.g. `fs/ntfs/MftAttributes.cpp`);
- **ambiguous** — several files bear it, so the citation names no single line;
- **missing** — no file bears it.

Measured today: **170 resolved, 0 ambiguous, 2 missing.** Zero ambiguity is what makes
basename resolution honest right now — and precisely why the gate must fail on ambiguity
rather than pick a winner, because that zero is a property of today's tree, not a law.

## Design decisions

**Ambiguity fails, and is not resolved by preference.** "Pick the one under `src/`" would
work today and would silently start citing the wrong file the day a second `BootSector.cpp`
appears. The gate reports the candidates and fails; the fix is to write the fuller path.

**Documenting a citation must not be making one — and this story found that out on
itself.** The first draft of the table above listed the six failures in citation syntax.
The gate would then have failed on *this file*, forever, for faithfully recording what it
was written to fix; and the same trap waits for any doc that discusses the notation,
including [quality-gates.md](../../testing/quality-gates.md) and the checklist rule below.
Two ways out were considered:

| Option | Verdict |
|--------|---------|
| An escape marker or an ignore comment the gate honours | Rejected. Every escape is a way to silence the gate, and the first person under time pressure uses it on a real citation. |
| **Write the file and the lines as separate columns, so no citation syntax appears** | **Taken.** Costs nothing, reads at least as well, and leaves the gate with no exception to be abused. |

The gate therefore has **no** escape hatch, which is only tenable because the alternative
is this cheap. The rule that follows is worth stating in the checklist alongside the
cite-by-symbol one: *to write about a citation, name the file and the lines separately.*

**The rule that goes with the gate is the half that actually removes the class.** One
sentence in [code-quality.md](../../code-quality.md)'s checklist: **cite code by symbol
name, because only the name survives a rebase.** The gate stops the class regrowing; the
rule stops it being created. A gate alone would just mean the same defect is now found by
CI instead of by a reviewer, four times a milestone, forever.

**Its limit is stated up front and in the gate's own documentation:** it catches a missing
path, an ambiguous name, and a range past EOF. It does **not** catch a citation pointing at
the wrong line of a file that happens to be long enough — which is the most common form
of the defect after a rebase. This gate makes a class of it impossible and leaves the rest
to the rule above. Claiming otherwise would be exactly the "confidently wrong about
itself" failure the milestone exists to remove.

**The six existing failures are fixed by rewriting the citations, not by relaxing the
gate.** Where the cited code still exists, the citation becomes a symbol name; where the
line range was load-bearing prose about a specific block, it is re-anchored. story-0602's
and story-0610's completed narratives are edited only in their citations — the findings
they record are history and stay as written.

**Scope is `docs/**.md`.** Not source comments: a `path:line` in a comment is rare here
and the parse is different. Not `AGENTS.md`/`CLAUDE.md`, which the pre-commit hook freezes
anyway.

## Acceptance criteria

- [x] `tools/lint/check_citations.py` resolves every citation in `docs/**.md`, in both
      forms: inline code holding a source path, a colon and a line or line range; and a
      markdown link whose target carries an `#L` line anchor.
- [x] It fails naming the doc, the citation, and the reason: missing / ambiguous / past EOF.
- [x] Ambiguous citations fail and list the candidates; none is chosen.
- [x] It refuses to pass having resolved zero citations
      ([story-0704](story-0704-vacuity-refusal-in-gate-files.md)).
- [x] Run on the tree as it stands, it fails with the six past-EOF and two missing
      citations above; run after the fixes, it passes. Demonstrated in that order.
- [x] All eight are fixed; every replacement cites a symbol where a symbol will do.
- [x] [code-quality.md](../../code-quality.md)'s checklist gains the cite-by-symbol rule.
- [x] `docs/testing/quality-gates.md` documents the gate **and what it cannot catch**.
- [x] CI runs it.

## Test plan

Unit (`tests/unit/lint/test_check_citations.py`), over a fixture tree so the cases do not
move when the real tree does:

- a citation whose path exists and whose range is inside the file → passes
- a range one line past EOF → fails, and the message names the line and the file length
- a bare basename matching exactly one file → resolves
- a bare basename matching two files → fails as ambiguous, listing both
- a basename matching nothing → fails as missing
- a path suffix (`fs/ntfs/Foo.cpp`) matching one file → resolves
- both citation syntaxes are recognised
- a docs tree containing no citations at all → exits non-zero (vacuity)

Integration: the gate over the real `docs/` passes at the end of this story. Recorded on
completion: the eight citations and what each became.

## Verified on completion (2026-08-08)

**The gate failed on the tree before the citations were fixed, and passes after — in that
order, which is the only order that proves anything.** Twelve unresolved before, zero
after: 171 citations across 113 documents all resolve.

**Every replacement cites a symbol where a symbol will do**, because only the name survives
a rebase. The four PartitionedWalk citations became `enumerateDisk`; the two carver ones
became the `carve` overrides they were always about; the session directory's default became
`kSessionDirectoryName`, which is where it moved when story-0702 collapsed the flag table;
and story-0608's two became `core/NameEscape.cpp`, which is the move that story argued for.

**The gate has no escape hatch and this document has none either.** Writing about the
notation means naming the file and the lines in separate columns, which is what the tables
above do — and what `quality-gates.md` and `code-quality.md` now do as well. An escape
marker was rejected when this was scoped: every escape is a way to silence the gate, and the
first person under time pressure uses it on a real citation.

**What it cannot catch, stated in the gate's own documentation:** a citation pointing at the
wrong line of a file that is long enough. That is the commonest form after a rebase and no
parse finds it. The gate stops the class regrowing; the rule beside it — cite by symbol —
stops it being created.

## Definition of Done

- [x] Acceptance criteria met, tests green (ASan + UBSan).
- [x] clang-format, clang-tidy, duplication, file-length guard clean.
- [x] `CHANGELOG.md` updated under `[Unreleased]`.
- [x] Epic row linked.
- [x] Story-level self-audit checklist ([code-quality.md](../../code-quality.md)) completed.
- [x] [code-quality.md](../../code-quality.md) and
      [quality-gates.md](../../testing/quality-gates.md) both updated.
