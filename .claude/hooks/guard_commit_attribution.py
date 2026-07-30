# SPDX-License-Identifier: GPL-3.0-or-later
"""Claude Code PreToolUse hook on Bash: block commits carrying AI attribution.

AGENTS.md §5: commit messages carry no tool/assistant attribution of any
kind. Default AI tooling likes to append Co-Authored-By / "Generated with"
footers; this hook rejects the commit before it happens. Exit 2 blocks the
command and feeds stderr back to the agent so it can rewrite the message.

Only inline messages are inspected (-m, heredocs). --amend of an already
tainted commit or -F <file> cannot be seen here; CI and review remain the
real gate.
"""

import re
import sys

from hook_common import bash_command, payload

# 'commit' must be the first non-flag token after 'git', so that
# `git log --grep "Co-Authored-By: Claude"` is not mistaken for a commit.
GIT_COMMIT = re.compile(r"\bgit(?:\s+-C\s+\S+|\s+-\S+)*\s+commit\b")

FORBIDDEN = (
    re.compile(r"co-authored-by:[^\n]*\b(claude|anthropic|ai\b)", re.IGNORECASE),
    re.compile(r"generated with[^\n]*\bclaude\b", re.IGNORECASE),
    re.compile(r"noreply@anthropic\.com", re.IGNORECASE),
)


def main():
    command = bash_command(payload())
    if not command or not GIT_COMMIT.search(command):
        return 0
    for pattern in FORBIDDEN:
        if pattern.search(command):
            sys.stderr.write(
                "Blocked: this commit message carries AI attribution "
                f"(matched {pattern.pattern!r}). AGENTS.md section 5 forbids "
                "any tool/assistant watermark in commits - rewrite the "
                "message without the footer and commit again.\n"
            )
            return 2
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception:  # noqa: BLE001 — a broken guard must not block real work
        sys.exit(0)
