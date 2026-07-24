<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# ADR-0009: Output safety — path confinement & bounded allocation

- **Status:** Accepted
- **Date:** 2026-07-24

## Context

Revenant reads attacker-influenced data: filenames and size fields come from deleted,
corrupt, or deliberately crafted on-disk structures. Two failure modes follow directly:

1. **Path traversal.** A recovered filename may contain `..`, absolute paths, drive
   letters, alternate separators, or NUL/control bytes. Writing it naïvely lets a
   malicious or corrupt name **escape the destination directory** and overwrite arbitrary
   files — a serious vulnerability, made worse because the tool often runs as root/admin.
2. **Allocation blow-up.** A size or count field read from disk (an MFT attribute length,
   a box size, a chunk length) may be enormous or malicious. Trusting it to size a buffer
   or vector is an out-of-memory denial of service.

## Decision

Two mandatory, tested rules across the codebase:

**Path confinement.** All output paths pass through a single `sanitizeOutputPath`
routine before any write. It:
- strips/neutralizes `..`, leading separators, drive/volume prefixes, and alternate
  separators; rejects NUL and control characters;
- maps names that are illegal or reserved on the target OS (`CON`, `PRN`, trailing dots
  or spaces on Windows, etc.) to safe equivalents;
- resolves the final path and **verifies it is contained within the destination root**,
  rejecting anything that escapes.
Reconstructed directory trees are recreated only *inside* the destination. There is no
code path that writes a recovered artifact using an unsanitized name.

**Bounded allocation.** No allocation is sized directly by an untrusted on-disk value.
Every size/count read from the device is range-checked against a sane, documented bound
(and against the remaining device/region size) before it drives an allocation or loop.
Exceeding the bound yields a typed error and a `Rejected`/`Uncertain` verdict — never a
large allocation or an unbounded loop.

## Consequences

- A crafted image cannot make Revenant write outside its destination or exhaust memory —
  both are covered by unit tests and by the parser fuzzers
  ([testing strategy](../testing/strategy.md), [SECURITY.md](../../SECURITY.md)).
- Sanitization can rename recovered files (e.g. a Windows-reserved name); the mapping is
  deterministic and recorded in the session manifest so the original name is not lost as
  information, only as a literal on-disk filename.
- A single choke-point (`sanitizeOutputPath`) keeps the rule enforceable and testable
  rather than scattered across sinks.
