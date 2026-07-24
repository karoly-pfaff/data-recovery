<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# ADR-0008: Resumable, checkpointed recovery

- **Status:** Accepted
- **Date:** 2026-07-24

## Context

Recovery runs are long: scanning a multi-terabyte device takes hours, and it often runs
against failing hardware where a cascade of bad sectors, a power loss, or an operator
`Ctrl-C` can interrupt the job at any point. Restarting from zero after an interruption is
unacceptable — both for time and because re-reading a dying disk can hasten its death.

## Decision

Recovery is **resumable by design**, built on the file-backed candidate index introduced
in [ADR-0006](adr-0006-candidate-arbitration-deferred-extraction.md):

- A **recovery session** has a durable, on-disk state directory (in the destination, not
  the source): the candidate index, the scan progress (how far the device has been
  scanned, per phase and partition), the bad-sector map, and the session manifest.
- Progress is **checkpointed** at bounded intervals and flushed on graceful cancellation
  (`Ctrl-C` triggers a clean flush, not an abort).
- Restarting with the same source and session directory **resumes**: already-scanned
  ranges are skipped, discovered candidates are retained, and only the unscanned remainder
  is processed before arbitration and extraction.
- The session state is itself untrusted-on-reload data: it is validated when resumed, and
  a corrupt/incompatible state is rejected with a clear message rather than misused.

Extraction is likewise resumable: because extraction is deferred until arbitration, a run
interrupted mid-extraction re-derives the winner set and continues writing the unwritten
winners.

## Consequences

- A killed or crashed run costs only the unscanned tail, not the whole device — the single
  biggest quality-of-life and safety improvement for real recoveries.
- The candidate index and scan cursor must be durable and crash-consistent (append-only
  with checkpoints), which constrains their on-disk format. This is a deliberate design
  requirement, tested including simulated interruption.
- Pause/resume and "scan now, extract later" fall out naturally from the same mechanism.
- Session state lives in the destination and never touches the read-only source
  ([ADR-0005](adr-0005-read-only-by-default.md)).
