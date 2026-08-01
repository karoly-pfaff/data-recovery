---
name: wsl-bench
description: Use when work needs Linux locally - GCC/libstdc++ diagnostics, libFuzzer runs, losetup loop devices (story-0603), soak or long-fuzz campaigns (story-0606), valgrind, or .deb verification. Documents this machine's Debian WSL2 workbench - how to reach it, what is installed, and the known traps.
---

# The Debian WSL2 workbench

This machine has a WSL2 **Debian 13 (trixie)** distro, normally stopped (the first
command boots it). 8 cores, 15 GB RAM, ~956 GB free on `/`. The repo is visible at
`/mnt/d/Projects/data-recovery`.

It is the only local environment for the things Windows cannot do:

- **GCC/clang against libstdc++** — the STL that produces the clang-tidy findings
  MSVC hides.
- **libFuzzer that actually links** — the Windows `fuzz` preset never links (CRT
  mismatch in `clang_rt.fuzzer`); Linux is where fuzzers run.
- **`losetup` loop devices** — `RawDevice`'s Linux half can be *run*, not only
  compiled (story-0603).
- **valgrind, `.deb` install checks, soak runs** (story-0606).

## Reaching it

Always from the **Bash tool**, not PowerShell (PowerShell 5.1 rejects `||` inside
the quoted command):

```bash
wsl.exe -d Debian -- bash -lc '<command>'
```

Privileged commands: `sudo` prompts for a password and cannot be answered
non-interactively. Run as root instead:

```bash
wsl.exe -d Debian -u root -- bash -lc '<command>'
```

Complex quoting degrades across the `wsl.exe` boundary — variable expansion inside a
`for` loop passed this way has come back empty. Pass simple commands, or write a
script file (visible under `/mnt/d/...`) and run that.

## What is installed (state as of 2026-07-30)

- g++ 14.2, cmake, ninja, pkg-config, python3.
- **No vcpkg, no GTest** — individual TUs compile, but the test binary does not
  build yet. Finishing provisioning (vcpkg + GTest) is what a local TSan, coverage,
  or full-suite run would need; the procedure belongs in `docs/install.md`.
- Verify clang before a fuzz session (`command -v clang++`); if absent, install it
  as root via apt.

## Reproducing a GCC-only diagnostic (cheapest check)

Compile one TU with the project's real warning contract:

```bash
wsl.exe -d Debian -- bash -lc 'cd /mnt/d/Projects/data-recovery && g++ -std=c++20 -O2 \
  -Wall -Wextra -Wpedantic -Wshadow -Wold-style-cast -Wcast-align -Wconversion \
  -Wsign-conversion -Wnull-dereference -Wdouble-promotion -Wformat=2 \
  -Wimplicit-fallthrough -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Werror \
  -Iinclude -Isrc -c src/<file>.cpp -o /tmp/o.o'
```

Special cases: `core/Version.cpp` needs `-DREVENANT_VERSION='"<version>"'`;
`carve/PrefilterAvx2.cpp` needs `-mavx2 -DREVENANT_HAVE_AVX2=1` or it compiles as a
stub.

## Loop devices (story-0603)

The whole pass is a script now — build, fixtures, attach, eight checks, teardown —
and it is what to run rather than hand-rolling `losetup`:

```bash
wsl.exe -d Debian -u root -- bash -lc \
  'python3 /mnt/d/Projects/data-recovery/tools/loopdev/verify_loop_device.py'
```

It exits with the number of checks that did not pass, and detaches its devices
even when one fails. `losetup` needs root, so `-u root` is not optional.

What this machine answers, measured rather than assumed: `losetup -P` scans the
table even though `/sys/module/loop/parameters/max_part` is `0`; `--sector-size
4096` is honoured, and at that size the kernel rescales the MBR's LBAs, so its
own partition scan is *not* a witness for ours; the default user is not in
`disk`, so the unprivileged case is genuinely refused.

The **unprivileged** case is a first-class test, not an inconvenience: the same open
as a user who cannot read the node must produce the actionable error M4 promised,
not a bare `EACCES`. Passing `--unprivileged-user root` is how to watch that check
refuse to certify itself.

## Traps

- Do not assume the distro state: it is a workbench, provisioned incrementally.
  `command -v <tool>` before relying on anything beyond the list above.
- Detach loop devices when done; a stale `/dev/loopN` from a previous session will
  confuse the next run.
- Windows-side gotchas (Device Guard blocking fresh binaries, nothing on PATH) do
  not apply inside WSL — if a binary "won't run" here, it is a real failure.
