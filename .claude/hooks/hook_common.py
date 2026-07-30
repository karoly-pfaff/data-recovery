# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared helpers for the Claude Code hook scripts in this directory.

Every hook receives a tool-event JSON payload on stdin and must be
best-effort: a malformed payload or missing field is a no-op, never an error.
"""

import json
import os
import sys
from pathlib import Path


def payload():
    try:
        data = json.load(sys.stdin)
    except ValueError:
        return {}
    return data if isinstance(data, dict) else {}


def tool_input(data):
    field = data.get("tool_input")
    return field if isinstance(field, dict) else {}


def edited_file(data):
    path = tool_input(data).get("file_path")
    return Path(path) if path else None


def bash_command(data):
    return tool_input(data).get("command")


def project_root():
    return Path(os.environ.get("CLAUDE_PROJECT_DIR") or Path.cwd()).resolve()


def inside_project(path, root):
    try:
        return path.resolve().is_relative_to(root)
    except OSError:
        return False
