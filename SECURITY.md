<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Security Policy

Revenant parses untrusted, often corrupt byte streams from damaged media, and it
frequently runs with elevated privileges to read raw devices. Security is therefore a
core correctness concern, not an afterthought. This policy explains what we consider a
vulnerability and how to report one.

## What counts as a vulnerability

- Any input (disk image, device bytes, filesystem metadata, or a candidate file) that
  causes a **crash, hang, unbounded memory growth, or out-of-bounds access** in a parser.
- Any path by which a **recovered or forged filename escapes the destination directory**
  (path traversal), or overwrites unintended files.
- Any way the tool **writes to the source device** outside an explicit, guarded write mode.
- Any unbounded allocation driven by an **attacker-controlled size field** read from disk.

These are treated as security bugs even though the tool is defensive: a recovery run on a
malicious image must not compromise the operator's machine.

## Reporting

For undisclosed issues, contact the maintainers privately before public disclosure. When
reporting a parser crash or hang, **include the triggering input** (a minimized sample is
ideal) so it can be added to the fuzz regression corpus.

Publicly-known robustness issues (e.g. a crash on an already-published sample) may be
filed as normal issues with the input attached.

## How we reduce risk (by design)

- **Read-only source by construction** — see
  [ADR-0005](docs/architecture/adr/adr-0005-read-only-by-default.md).
- **Total parsers** — every byte-parser has a libFuzzer target and must never crash,
  hang, or read out of bounds; this is a merge gate
  ([testing strategy](docs/testing/strategy.md)).
- **Bounded allocation** — sizes read from disk are range-checked before they drive any
  allocation; a corrupt length can never trigger a huge allocation.
- **Output path sanitization** — recovered names are sanitized and confined to the
  destination directory; traversal sequences and absolute paths are neutralized.
- **Sanitizers in CI** — ASan + UBSan (and TSan for concurrency) run on every change.
- **Supply chain** — CI actions and tool versions are pinned; see story-0008.

## Scope

This policy covers the `librevenant` core and the `revenant-carve` / `revenant-undelete`
frontends. Third-party dependencies should be reported to their respective projects,
though we welcome a heads-up so we can pin or patch.
