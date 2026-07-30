---
name: fuzz-campaign
description: Use when running a long libFuzzer campaign against the byte parsers (story-0606), when a fuzzer reports a crash/OOM/timeout that needs triage, or when promoting inputs into the curated corpus under tests/fuzz/corpus/. Also covers where fuzzers can and cannot run on this machine.
---

# Long fuzz campaigns

The fuzz targets live in `tests/fuzz/*Fuzz.cpp`, one per byte parser, each with
a curated corpus directory `tests/fuzz/corpus/<Target>/`. CI runs them for
seconds; a campaign runs them for **hours per target**, in the background, with
every finding triaged to a fix and a corpus entry.

## Where to run

- **WSL Debian is the primary bench** — libFuzzer links there and behaves.
  Invoke **wsl-bench** for how to reach it and its traps; verify `clang++`
  exists before starting (`command -v clang++`, install via apt as root if not).
- **Windows is the fallback only.** The `fuzz` preset never links here (CRT
  mismatch); a special RelWithDebInfo recipe exists but produces a **bogus
  `container-overflow`** on any target that grows a string/vector. Never trust
  a Windows-recipe crash until it reproduces under the `debug` preset or on
  Linux.

## Campaign loop (per target)

1. **Build** the fuzzers on the bench (`fuzz` preset on Linux).
2. **Launch in the background** — never block a session on a campaign:

   ```bash
   ./<Target> tests/fuzz/corpus/<Target>/ \
     -max_total_time=14400 -rss_limit_mb=2048 -timeout=10 \
     -artifact_prefix=<scratch>/artifacts/<Target>/ \
     -print_final_stats=1 > <scratch>/<Target>.log 2>&1
   ```

   Use the Bash tool's background mode (or `nohup` inside WSL) and check the
   log periodically; `-fork=<cores>` parallelizes a single target.
3. **Triage every artifact**, oldest first:
   - Deduplicate by the top of the stack trace, not by input bytes.
   - **Replay under the `debug` preset first** — this separates real findings
     from bench-recipe false positives, and gives an ASan report worth reading.
   - Minimize: `./<Target> -minimize_crash=1 -runs=10000 <artifact>`.
4. **Fix test-first**: turn the minimized artifact into a failing unit test
   (byte fixture in `tests/unit/`), then fix the parser. The fuzzer found it;
   a unit test keeps it found.
5. **Promote the corpus** when the target's campaign ends:

   ```bash
   ./<Target> -merge=1 tests/fuzz/corpus/<Target>/ <campaign-corpus-dir>/
   ```

   Commit the merged corpus (small, minimized inputs only) with the story.
6. **Record it**: hours run, findings, and fixes go in the story file — a
   campaign that found nothing is evidence too, but only if written down.

## Rules

- One story governs the campaign (story-0606 for the M6 sweep); fixes for
  crashes it finds reference that story.
- An OOM or timeout is a finding, not noise — the parsers face hostile bytes
  with the founding claim that they never run away
  (docs/architecture/adr/adr-0003).
- Never let artifacts pile up untriaged; a campaign is done when its artifact
  directory is empty and its corpus is merged, not when the timer runs out.
