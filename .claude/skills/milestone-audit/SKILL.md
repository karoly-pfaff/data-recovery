---
name: milestone-audit
description: Use at a milestone boundary, after the last story merges and before the next milestone's stories are finalized. Runs the milestone-level architecture audit from docs/code-quality.md as a multi-agent adversarial workflow - survey lenses, refutation pass, synthesis into story and gate proposals.
---

# Milestone architecture audit

`docs/code-quality.md` ("Milestone-level architecture audit") requires this review
of the whole increment at every milestone boundary, and marks it as a multi-agent
adversarial pass. This skill is that pass. Findings become stories; recurring
findings become proposals for new automated gates.

## 1. Establish the increment

- `git tag --list 'v*' --sort=version:refname` — the previous milestone's tag is
  `baseRef`; the increment is `baseRef..HEAD` (or the just-tagged release).
- Name the milestone being closed (e.g. `M5`).

## 2. Run the workflow

Invoke the Workflow tool with the companion script:

```
Workflow({
  scriptPath: '.claude/skills/milestone-audit/workflow.js',
  args: { milestone: 'M5', baseRef: 'v0.2.0' }   // headRef defaults to HEAD
})
```

It spawns at most 13 agents: four survey lenses (layer leakage, ADR conformance,
complexity creep, recurring findings), an adversarial refutation pass over the top
findings (capped at 8, logged if anything passes through unverified), and one
synthesizer. The result contains confirmed findings, the refuted ones with reasons,
an audit summary, story proposals, and gate proposals.

## 3. Act on the result

- **Confirmed findings → stories.** Draft them into the *next* milestone's epic as
  titles (numbers are allocated when the story file is written — see
  `docs/backlog/README.md`). A finding serious enough to block the next milestone
  is raised to the maintainer instead of quietly queued.
- **Audit summary → the closing epic.** Append a short "Architecture audit" note to
  the closed milestone's epic file recording what was found (the M4 audit finding
  that became story-0601 is the precedent).
- **Gate proposals → the maintainer.** A new automated check is a process change;
  propose it, do not wire it in unasked.
- Refuted findings are recorded nowhere except the workflow result — do not carry
  them forward.
