# SPDX-License-Identifier: GPL-3.0-or-later
"""Claude Code PostToolUse hook: clang-format the just-edited C++ file.

Best-effort by design: any missing prerequisite exits 0 silently. Blocking an
edit over formatting is the pre-commit hook's and CI's job, not this hook's.
"""

import glob
import os
import shutil
import subprocess
import sys
from pathlib import Path

from hook_common import edited_file, inside_project, payload, project_root

CPP_SUFFIXES = {".cpp", ".hpp", ".cc", ".h"}


def candidate_paths():
    """Known install locations, preferred over PATH: pip first (the CI-pinned
    version), then the winget LLVM install."""
    candidates = []
    appdata = os.environ.get("APPDATA")
    if appdata:
        pattern = os.path.join(appdata, "Python", "Python3*", "Scripts", "clang-format.exe")
        candidates.extend(sorted(glob.glob(pattern), reverse=True))
    candidates.append(str(Path.home() / ".local" / "bin" / "clang-format"))
    candidates.append(r"C:\Program Files\LLVM\bin\clang-format.exe")
    return candidates


def find_clang_format():
    for candidate in candidate_paths():
        if os.path.isfile(candidate):
            return candidate
    return shutil.which("clang-format")


def main():
    target = edited_file(payload())
    if target is None or target.suffix.lower() not in CPP_SUFFIXES:
        return
    if not target.is_file() or not inside_project(target, project_root()):
        return
    tool = find_clang_format()
    if tool is None:
        return
    subprocess.run([tool, "-i", "--style=file", str(target)],
                   check=False, capture_output=True, timeout=30)


if __name__ == "__main__":
    try:
        main()
    except Exception:  # noqa: BLE001 — best-effort hook, never fail the edit
        pass
    sys.exit(0)
