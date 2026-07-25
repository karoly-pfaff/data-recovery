<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# STORY-0057: CI supply-chain hardening

- Epic: [epic-m0-foundation](../epic-m0-foundation.md)
- Status: Done
- Size: S

## Goal

Pin every third-party executable CI runs to an immutable identity: actions by commit
SHA, vcpkg by commit, npm tools by lockfile.

## Acceptance criteria

- [x] No `uses:` in `.github/` references a tag or branch ref — every third-party
      action is pinned to a full 40-hex commit SHA, with the human-readable
      version kept as a trailing comment.
- [x] vcpkg is cloned and then checked out at a pinned commit (`VCPKG_COMMIT`), not
      whatever `HEAD` of `microsoft/vcpkg` happens to be on the day CI runs.
- [x] `jscpd` is installed via `npm ci` from a committed lockfile (`package-lock.json`)
      — no floating `npx <pkg>@version` fetch that re-resolves the dependency tree
      on every run.
- [x] Every `actions/checkout` step sets `with: persist-credentials: false` (the
      checked-out `GITHUB_TOKEN` is never persisted in the git config of the
      runner's clone).
- [x] Workflow-level `permissions: contents: read` confirmed with no job-level
      escalation anywhere in `ci.yml`.
- [x] `SECURITY.md` reviewed as current against this change (see "SECURITY.md
      verification" below).

## Resolved pins

Every SHA below was resolved live via `git ls-remote` against the upstream
repository immediately before editing the workflow (transcript in
`task-8-report.md`) — none were hand-typed from memory.

| Dependency | Ref requested | Resolved tag | Commit SHA |
|---|---|---|---|
| `actions/checkout` | newest stable `v4*` | `v4.4.0` | `11d5960a326750d5838078e36cf38b85af677262` |
| `lukka/get-cmake` | `v3.30.5` | `v3.30.5` | `b516803a3c5fac40e2e922349d15cdebdba01e60` |
| `ilammy/msvc-dev-cmd` | newest stable `v1.x` | `v1.13.0` | `0b201ec74fa43914dc39ae48a89fd1d8cb592756` |
| `microsoft/vcpkg` | newest `2025*` release | `2025.12.12` | `84bab45d415d22042bd0b9081aea57f362da3f35` |

`actions/checkout` and `lukka/get-cmake` are lightweight tags (the `git ls-remote`
SHA is the commit object itself, no `^{}` line). `ilammy/msvc-dev-cmd` and
`microsoft/vcpkg` tags in the queried ranges are also lightweight for the specific
tags selected here (`v1.13.0` and `2025.12.12` have no `^{}` dereferenced line in
the `ls-remote` output); the SHA recorded is the tag's own object SHA in both
cases — verified against the raw `ls-remote` output in `task-8-report.md`, not
assumed.

## Test plan

- CI itself is the test: all five jobs (`guards`, `build-test` x2 OS,
  `tidy`, `coverage`, `fuzz-smoke`) run green from pinned inputs on the next push.
- `grep -rEn "uses:" .github` proves no unpinned `uses:` remains: every third-party
  line matches a 40-hex SHA followed by a version comment; the only non-SHA form
  left is the local composite path (`./.github/actions/setup-vcpkg`).
- `npx --yes yaml-lint .github/workflows/ci.yml .github/actions/setup-vcpkg/action.yml`
  confirms both edited YAML files still parse (pyyaml was not installed locally;
  see "Known issues" for why this fallback was used instead of
  `python -c "import yaml..."`).
- `npm ci --ignore-scripts --dry-run` succeeds against the generated
  `package-lock.json` (119 packages resolved, matching `jscpd@4.0.5`'s dependency
  tree; no `node_modules/` written by `--dry-run` or by the earlier
  `--package-lock-only` generation step).
- `ctest --preset debug --output-on-failure` — 58/58, proving this CI/docs-only
  change touched no C++ and left the existing tree green.

## SECURITY.md verification

`SECURITY.md` (written at foundation, before any story existed) already states
"Supply chain — CI actions and tool versions are pinned; see story-0057" under
"How we reduce risk (by design)". Re-read in full for this story: every claim
still holds (read-only-by-default via ADR-0005, fuzz targets as a merge gate,
bounded allocation, output path sanitization, sanitizers in CI) and the
supply-chain line now describes the state this story produces. **No change
needed** — the epic's "add SECURITY.md" outcome was already satisfied at
foundation; this story's job was only to confirm it stayed current, and it did.

## Definition of Done

- [x] Acceptance criteria met, tests green (ctest 58/58 — see test plan; no
      C++ changed, so this proves the tree is untouched, not new coverage).
- [x] Coverage held or raised (>= 85% core). Not applicable/not measured: this
      story changes zero `src`/`include` bytes (YAML, JSON lockfile, and docs
      only), so the core-coverage figure cannot move either direction — same
      framing as story-0006's coverage-gate story.
- [x] clang-format, clang-tidy, duplication, file-length guard clean. No C++
      changed, so these are not re-run beyond the single `ctest` proof above;
      the duplication tool itself (`jscpd`) is exactly what this story
      re-plumbed onto a locked npm install rather than a floating `npx` fetch.
- [x] CHANGELOG.md updated under `[Unreleased] / Security`.
- [x] Story-level self-audit checklist (docs/code-quality.md) completed (below).
- [x] Docs/ADRs updated if the design changed. No architectural design change;
      `SECURITY.md` reviewed and confirmed current (see above), no edit needed.

## Known issues

1. **pyyaml unavailable locally.** `python -c "import yaml"` failed
   (`ModuleNotFoundError: No module named 'yaml'`) in this dev environment,
   and `python3` isn't on `PATH` at all (only the Microsoft Store shim, and a
   separate `python` that resolves to 3.13.5 without pyyaml). Installing pyyaml
   was out of scope for a YAML/docs-only story with a "keep it fast and lean"
   constraint, so the brief's documented fallback was used instead:
   `npx --yes yaml-lint` against both edited YAML files, which reported
   `YAML Lint successful.` for both. This validates syntax (the files parse as
   YAML), not GitHub Actions workflow semantics; the real semantic check is the
   next CI run itself, per the test plan above.
2. **`npm install --package-lock-only` reported "audited 120 packages"** even
   though the brief's Step 4 text says `npm install --ignore-scripts` (no
   `--package-lock-only`). `--package-lock-only` was added deliberately (per the
   task instructions given for this story, which supersede the older brief
   text) specifically so the command cannot write `node_modules/` even
   transiently — confirmed by checking `ls node_modules` immediately after
   the command returned "No such file or directory" both then and after the
   later `npm ci --dry-run`. `package-lock.json` was generated correctly either
   way (119 resolved packages for `jscpd@4.0.5`'s dependency tree, one fewer
   than the "120 packages" the resolver step audits — the difference is the
   root `revenant-devtools` package itself, which `npm ci`'s package count
   excludes but `npm install`'s audit count includes).

## Story-level self-audit (docs/code-quality.md)

- Responsibility & clarity: N/A for most items — no new functions or types.
  The two touched YAML files each keep their single existing responsibility
  (`ci.yml` orchestrates jobs; `setup-vcpkg/action.yml` clones and bootstraps
  vcpkg) — pinning inputs didn't add a second responsibility to either.
- Design: no new abstractions introduced (YAGNI holds trivially — this is
  configuration hardening, not new code). No duplicated knowledge: the vcpkg
  commit is named once (`VCPKG_COMMIT`, workflow-level `env:`) and consumed by
  reference in the composite action, not repeated per job.
- Anti-patterns: none introduced — no God object risk in CI config; no
  behavior change to any job's actual build/test steps, only to which
  immutable commit each third-party tool resolves to.
- Correctness & safety: this story is entirely about closing a class of
  supply-chain failure (a tag or branch ref can be force-moved or repointed by
  the upstream maintainer or via a compromised account; a SHA cannot). The
  source-device read-only guarantee is unrelated and untouched. No byte
  parsing involved.
- Tests: CI configuration has no unit-test seam of its own; the test plan
  above (grep, YAML parse, `npm ci --dry-run`, full `ctest` run) is the
  practical equivalent, and the real end-to-end proof is the next CI run on
  this branch/PR. No fuzz target applies (no byte-parser touched).
