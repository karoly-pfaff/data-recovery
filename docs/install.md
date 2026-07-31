<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Development environment setup

Everything you need to build Revenant, run its tests, and reproduce every CI gate
locally. [`CONTRIBUTING.md`](../CONTRIBUTING.md) points here for setup; this page is the
concrete, copy-pasteable version.

If you only want to build and test, you need the **core** tools — which include
`lizard`, because the gate scripts' own unit tests run under `ctest` and one of
them imports it. The **gate** tools matter before you open a PR: CI runs them and
a red gate does not merge. The **fuzzing** tools are needed only when you touch a
byte parser.

## What you need

| Purpose | Tool | Version | Notes |
|---------|------|---------|-------|
| Core | C++20 compiler | MSVC 2022+, GCC 13+, or Clang 16+ | |
| Core | CMake | ≥ 3.25 | presets v6 |
| Core | Ninja | any | the generator every preset uses |
| Core | vcpkg | any recent | supplies GoogleTest; `VCPKG_ROOT` **must** be set |
| Core | Python | 3.x | lint/guard scripts |
| Gate | clang-format | **22.1.8** | pinned — see below |
| Gate | clang-tidy | **22.1.8** | pinned — see below |
| Core | `lizard` | **1.23.0** | pinned — the duplication detector; `ctest` runs its unit tests, so a build-and-test needs it too |
| Fuzzing | Clang compiler | any recent | libFuzzer; the `fuzz` preset requires `clang++` |

**Why the clang tools are version-pinned.** Formatting and check behaviour differ
across clang majors, so an unpinned local tool disagrees with CI and `format-check`
flaps. Install them from the PyPI wheels, which are the same artifacts CI uses —
not from your distro's package manager.

**Why `lizard` is pinned.** The duplication gate reads a block's token length off
the duplicate extension's own node indices, which no API version number
announces. It must be importable by the same interpreter that runs the gate, so
install it with `pip`, not `pipx` — and on a PEP 668 distribution (Debian 12+,
Ubuntu 23.04+, and the GitHub runners) that means `--break-system-packages`, or
a virtualenv the gate's interpreter is inside.

## Windows

Nothing below needs a manual PATH edit except where noted.

```powershell
# Compiler + a bundled CMake and Ninja (skip if you already have VS 2022+)
winget install --id Microsoft.VisualStudio.2022.BuildTools

# Clang, for the fuzz preset
winget install --id LLVM.LLVM

# Pinned analysis tools
pip install clang-format==22.1.8 clang-tidy==22.1.8 lizard==1.23.0

# vcpkg
git clone --depth 1 https://github.com/microsoft/vcpkg "$env:USERPROFILE\vcpkg"
& "$env:USERPROFILE\vcpkg\bootstrap-vcpkg.bat" -disableMetrics
```

Three directories are **not** on `PATH` after this, and you need all three:

| What | Where |
|------|-------|
| `cmake.exe`, `ctest.exe` | `…\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin` |
| `ninja.exe` | `…\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja` |
| `clang-format.exe`, `clang-tidy.exe` | `%APPDATA%\Python\Python3xx\Scripts` |

The presets also require `VCPKG_ROOT`, and the MSVC build needs the compiler
environment. The reliable way to get all of it at once is a small wrapper that
sources `vcvars64.bat` and prepends those paths:

```bat
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\<ver>\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set "VCPKG_ROOT=%USERPROFILE%\vcpkg"
set "PATH=<cmake-bin>;<ninja-dir>;%APPDATA%\Python\Python3xx\Scripts;C:\Program Files\LLVM\bin;%PATH%"
%*
```

Then run every command through it: `dev.cmd cmake --preset debug`.

Alternatively install CMake and Ninja standalone (`winget install Kitware.CMake
Ninja-build.Ninja`), which do put themselves on `PATH`, and set `VCPKG_ROOT` as a
persistent user environment variable.

## Linux

```bash
sudo apt-get install -y build-essential cmake ninja-build python3 clang
pipx install clang-format==22.1.8
pipx install clang-tidy==22.1.8
python3 -m pip install --break-system-packages lizard==1.23.0

git clone --depth 1 https://github.com/microsoft/vcpkg ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
export VCPKG_ROOT=~/vcpkg   # add to your shell profile
```

## A Linux bench, if you develop on Windows

**Not optional.** Three classes of failure are invisible to an MSVC build and reach you
as a red CI run instead: diagnostics libstdc++ produces and the MSVC STL does not
(`-Wnull-dereference` inside `<streambuf>`, for one), clang-tidy checks that only fire
against real POSIX headers (`misc-no-recursion`), and clang-only warnings such as
`-Winvalid-utf8`, which rejects a stray cp1252 byte in a comment that both MSVC and GCC
compile in silence. Building on Linux before you call a change done is cheaper than
finding out from CI.

WSL2 is enough. From an elevated PowerShell, `wsl --install -d Debian`, then inside it:

```bash
sudo apt-get update
sudo apt-get install -y build-essential clang clang-tidy cmake ninja-build \
                        pkg-config python3 libgtest-dev
```

No vcpkg is needed here: `find_package(GTest CONFIG)` resolves against Debian's own
package, so the whole suite configures and builds directly.

```bash
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Debug -DREVENANT_BUILD_TESTS=ON
cmake --build build/linux --target revenant_tests
./build/linux/tests/revenant_tests

# clang-tidy, authoritative for the POSIX-only sources Windows cannot even parse
clang-tidy -p build/linux src/<file>.cpp
```

Add `lvm2` and `btrfs-progs` if you touch the destination-identity code — the layouts
that matter there (LVM, btrfs, overlay) cannot be built without them. Add `valgrind` for
the soak work. The repository is visible from WSL at `/mnt/<drive>/...`.

Two traps worth knowing before you lose an hour to them: `sudo` cannot be answered
non-interactively, so run privileged commands as `wsl.exe -d Debian -u root -- bash -lc
'…'`; and variable expansion degrades across the `wsl.exe` boundary, so a `for` loop or
`$var` inside the quoted command comes back empty — write a script file and run that.

## Verify the setup

The first configure builds GoogleTest from source via vcpkg, so it takes a few
minutes. Later ones are cached.

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

Then the gates, which are what CI actually enforces:

```bash
cmake --build --preset debug --target format-check
cmake --build --preset debug --target guard-limits
cmake --build --preset debug --target tidy      # see the Windows caveat below
cmake --build --preset debug --target duplication
```

Optionally enable the versioned pre-commit hook so the fast gates run on every
commit: `git config core.hooksPath .githooks`.

## Fuzzing

The `fuzz` preset builds every libFuzzer target with Clang:

```bash
cmake --preset fuzz
cmake --build --preset fuzz
build/fuzz/tests/fuzz/MftRecordFuzz -max_total_time=60 tests/fuzz/corpus/MftRecordFuzz
```

Always pass the corpus directory — the seeds in `tests/fuzz/corpus/` put the fuzzer
inside the parser instead of leaving it to guess magic bytes. Regenerate them with
`python3 tools/fuzz/make_seed_corpus.py`.

## Known platform caveats

**`tidy` fails on Windows with `invalid argument '-MDd' not allowed with
'-fsanitize=address'`.** The `debug` preset enables MSVC's AddressSanitizer, and
clang-tidy cannot consume that combination from the compile database. CI runs
clang-tidy on Linux, so this is local-only. Run the target from the sanitizer-free
`release` preset instead; the recipe lives with the gates, in
[quality-gates.md](testing/quality-gates.md).

**The `fuzz` preset does not link on Windows.** Clang ships `clang_rt.fuzzer` built
against the release CRT and without the MSVC STL's ASan container annotations, so
linking it into a debug, annotated build fails with `/failifmismatch` on
`_ITERATOR_DEBUG_LEVEL`, `RuntimeLibrary`, or `annotate_string`. Fuzzing is a
Linux-CI gate; if you need to run a target on Windows, compile it directly:

```powershell
clang++ -fsanitize=fuzzer,address -D_DISABLE_STRING_ANNOTATION=1 -D_DISABLE_VECTOR_ANNOTATION=1 `
        -O1 -g -std=c++20 -I include -I src `
        tests/fuzz/MftRecordFuzz.cpp src/fs/ntfs/*.cpp src/fs/NameDecode.cpp src/core/ByteReader.cpp `
        -o MftRecordFuzz.exe
```

Running it needs the ASan runtime on `PATH`:
`C:\Program Files\LLVM\lib\clang\<major>\lib\windows`. Without it the process exits
immediately with `0xC0000135` (DLL not found) and no message.

**The first vcpkg configure fails with a toolchain error.** `VCPKG_ROOT` is unset —
`CMakePresets.json` interpolates it into `CMAKE_TOOLCHAIN_FILE`, so an empty value
produces a path that does not exist.
