---
name: start-story
description: Use when starting work on a new story - picking the next one from the open milestone's epic, turning an epic table row into a story file, or when implementation is about to begin and no story has Status In progress. Counterpart of finish-story.
---

# Start a story

Run these steps **in order**. The Done path (finish-story) is mechanical; this
makes the start path mechanical too — numbering, template, branch, and the TDD
kickoff are rules, not memory. `docs/backlog/README.md` is the authority on
numbering; when in doubt, read it, do not recall it.

## 0. Preconditions

- Working tree clean, on `main`, freshly pulled.
- One story at a time: if a story file already says `In progress`, finish or
  park that one first.
- At a milestone boundary (previous milestone's last story merged, this one's
  stories not yet finalized): run **milestone-audit** first — its findings may
  become stories in this milestone.

## 1. Pick the story

Open the current milestone's epic (`docs/backlog/epic-m<N>-*.md`) and take the
next story in table order, unless the maintainer named one.

## 2. Allocate the number

- If the epic's table already assigns the story a number (the open milestone's
  tables do), use it.
- Otherwise: `MM` = milestone, `NN` = next unused position in this milestone.
  Check both the epic table and `docs/backlog/stories/story-MM*` — the file, not
  the table, is what claims a number.
- Never reuse a retired number. A story that landed wrong and was reverted is
  **reopened under its own number**, not replaced by a new one.

## 3. Write the story file

`docs/backlog/stories/story-MMNN-<slug>.md`, using the template in
`docs/backlog/README.md` ("Story template"). Filled examples of the quality
bar: `story-0001`, `story-0602`.

- Start at `Status: Backlog`. It becomes `Ready` only when every acceptance
  criterion is observable/testable and the test plan names concrete cases.
  **Do not start an un-Ready story** — sharpen the criteria instead; raise real
  open questions to the maintainer rather than guessing.
- Link the file from the epic's story table.

## 4. Branch and status

```bash
git checkout -b story/MMNN-<slug>   # cut from up-to-date main
```

Set `Status: In progress` in the story file (commit this on the branch).

## 5. First failing test

Before any production code, write the first failing test from the test plan
under `tests/` and watch it fail. From here
superpowers:test-driven-development governs the loop.

If the story adds a carve format, invoke **add-format-carver** instead of
hand-rolling the parser.

## Red flags — stop and go back

| Thought | Reality |
|---------|---------|
| "I'll write the story file after the code" | No code without a story. The file comes first; that is how YAGNI is enforced here. |
| "This NN slot is free, I'll reuse it" | Retired numbers stay retired. Old references must never silently point at new work. |
| "The criteria are vague but I get the idea" | An un-Ready story produces unreviewable work. Sharpen it or ask. |
| "Small change, I'll do it on main" | Never. Everything arrives via a `story/` or `fix/` branch (docs/git-workflow.md). |
