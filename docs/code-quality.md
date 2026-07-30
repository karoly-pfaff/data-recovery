<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Code Quality Standard

This document is the rationale and working checklist behind the rules summarized in
[`AGENTS.md`](../AGENTS.md). `AGENTS.md` is the binding contract; this is the *why* and
the *how you check it*. Where they appear to differ, `AGENTS.md` wins and this file is
the bug.

## The Prime Directive

> **A function does exactly one thing, at exactly one level of abstraction.**

If describing a function needs the word "and", split it. A function that opens a device,
parses a header, and writes output is three functions. This is the single most important
rule in the project because it is what keeps low-level byte code debuggable: small,
single-purpose functions fail in obvious places.

The [hard limits](../AGENTS.md#2-hard-limits-enforced-by-clang-tidy--ci-scripts) are the
*mechanical floor* of this rule. Passing them is necessary, not sufficient — a function
inside every limit can still do three things. The self-audit below checks the part
machines cannot.

## Principles we hold ourselves to

- **SRP (Single Responsibility).** Every type and function has one reason to change.
- **DIP (Dependency Inversion).** Depend on interfaces (`BlockDevice`, `FormatCarver`,
  `FileSystem`), not concretes. This is what makes the code testable.
- **DRY.** No duplicated logic. The duplication detector fails CI on clones at the
  threshold [quality-gates.md](testing/quality-gates.md) records. DRY is about
  *knowledge*, not textual similarity — don't fold together two things that merely look
  alike but change for different reasons.
- **YAGNI.** No code without a story. No speculative generality, unused parameters, or
  "flexible" hooks nobody calls. Delete dead code on sight.
- **Fail loud, fail typed.** Errors are `Result<T>` values. No empty `catch`, no ignored
  returns, no silent fallback that hides a fault. A recovery tool that quietly does the
  wrong thing is worse than one that stops.
- **No magic values.** Name constants (`kSectorSize`, not `512`).
- **Least surprise.** A reader should predict what a function does from its name and
  signature. If they can't, rename or split it.

## Design patterns & anti-patterns

Patterns are tools, not goals. Use the simplest thing that works; reach for a pattern
only when it removes real duplication or coupling.

- **Encouraged where they fit:** Strategy (carvers/filesystems behind interfaces),
  Registry (`CarverRegistry`), Decorator (`CachingDevice`, `RetryingDevice`), Factory
  (device/filesystem construction), Visitor (entry enumeration).
- **Watched for and rejected in review:** God object, feature envy, primitive obsession
  (pass a `SectorSize` type, not a bare `uint32_t` where it matters), shotgun surgery,
  boolean-parameter traps, deep nesting, output parameters where a return value fits,
  and premature abstraction.

## Automated gates (see [quality-gates.md](testing/quality-gates.md))

Everything in this document that can be checked by a machine is, on every pull request.
What the gates are and what each one fails on is owned by
[quality-gates.md](testing/quality-gates.md); the commands to reproduce them on a fresh
machine are in [install.md](install.md). They are non-negotiable and cannot be merged
around.

## The story-level self-audit (human/AI)

Run this checklist for **every story** before marking it Done. It covers what the tools
cannot. Answer each honestly; a "no" is rework, not a note-to-self.

### Responsibility & clarity
- [ ] Does every new/changed function do exactly one thing at one abstraction level?
- [ ] Can each function's purpose be understood from its name and signature alone?
- [ ] Is every file focused on one responsibility, not merely under the length limit?

### Design
- [ ] Do new types have one reason to change (SRP)?
- [ ] Do consumers depend on interfaces, not concretes (DIP)?
- [ ] Is any introduced abstraction actually used now (YAGNI), not "for later"?
- [ ] Is there any duplicated *knowledge* the tools didn't catch?

### Anti-patterns
- [ ] No God object, feature envy, boolean traps, or deep nesting introduced?
- [ ] No premature generality, dead code, or commented-out code?

### Correctness & safety
- [ ] Are all error paths handled as typed values — nothing swallowed?
- [ ] Is the source-device read-only guarantee intact?
- [ ] Is byte handling UB-free (spans, `bit_cast`, bounds checks)?

### Tests
- [ ] Written test-first; do tests cover malformed/edge inputs, and every boundary from
      both sides?
- [ ] Does every new byte-parser have a fuzz target?

## Milestone-level architecture audit

At each milestone boundary, review the whole increment against the accepted ADRs:

- Has any layer leaked responsibility into another?
- Do the interfaces still hold, or did reality demand a new seam (→ new ADR)?
- Has complexity crept in that a refactor should remove before widening scope?
- Are there recurring review findings that should become a new automated check?

Findings become stories. The `milestone-audit` skill (`.claude/skills/`) runs
this audit as a multi-agent adversarial pass — survey lenses, refutation,
synthesis into story and gate proposals.
