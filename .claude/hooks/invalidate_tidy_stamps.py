# SPDX-License-Identifier: GPL-3.0-or-later
"""Claude Code PostToolUse hook: a header edit invalidates build/*/tidy-stamps.

The tidy target's per-file stamps depend on the source file and the tidy
configs but not on included headers (see cmake/DevTargets.cmake), so after a
header change a green `tidy` run can be stale: a TU that relied on the old
header still shows as passing. Deleting the stamps forces a full re-check.
"""

import shutil
import sys

from hook_common import edited_file, inside_project, payload, project_root

HEADER_SUFFIXES = {".hpp", ".h"}


def main():
    target = edited_file(payload())
    if target is None or target.suffix.lower() not in HEADER_SUFFIXES:
        return
    root = project_root()
    if not inside_project(target, root):
        return
    for stamps in (root / "build").glob("*/tidy-stamps"):
        shutil.rmtree(stamps, ignore_errors=True)


if __name__ == "__main__":
    try:
        main()
    except Exception:  # noqa: BLE001 — best-effort hook, never fail the edit
        pass
    sys.exit(0)
