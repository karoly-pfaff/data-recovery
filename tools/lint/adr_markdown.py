#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Turning a markdown file into the lines a reader sees, the way git counts them.

The bottom of story-0705's stack, and the layer that earned its own module: three
audit rounds found defects here and not one of them was about ADRs. Reading a
file and reading a *record* are different jobs — `adr_document` does the second.

Two rules, both learned the hard way:

- **What a reader sees speaks for the file.** Fenced blocks are examples, and an
  example is not a declaration; comments are invisible, and invisible text that
  the gate treats as prose is worse than an example, because it renders as
  nothing at all.
- **A construct that swallows the rest of the file is a fault, not a silent
  answer.** An unterminated fence or comment exits 2 rather than blanking the
  tail — the blanked tail is how a whole document quietly said nothing.
"""
from __future__ import annotations

import re

# The delimiter is captured so a closing fence can be matched to *its* opener.
# A boolean toggled by any fence line cannot nest: an ADR illustrating the ADR
# template with a ````-fenced example containing ``` blocks would un-blank the
# inner text, and a `**Supersedes:**` in the example would read as a
# declaration — the exact defect fencing was introduced to close.
CODE_FENCE = re.compile(r"^\s*(`{3,}|~{3,})")

# An HTML comment is invisible to a reader and was prose to this gate. Every ADR
# opens with one, so the cover was idiomatic in every file in the tree.
COMMENT_OPEN = "<!--"
COMMENT_CLOSE = "-->"
COMMENT_SPAN = re.compile(re.escape(COMMENT_OPEN) + r".*?" + re.escape(COMMENT_CLOSE))


class CannotAnswer(Exception):
    """The question cannot be answered from what is here. Never a pass.

    Named for the condition rather than for the caller: a malformed document
    and a malformed range are the same kind of event, and both must exit 2.
    """


def lines_of(text: str) -> list[str]:
    """The lines *git* sees: separated by `\\n`, and nothing else.

    `str.splitlines()` also breaks on U+2028, U+2029, U+0085, `\\v`, `\\f` and
    `\\x1c`-`\\x1e`; git counts `\\n`. One such character above a frozen heading
    shifted every span past the hunk numbers git reports, and `docs/` is outside
    the encoding gate's roots so nothing rejects it first. A trailing newline
    terminates the last line rather than starting one, as git numbers it too.
    """
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    return lines


def visible_lines(text: str, path: str = "") -> list[str]:
    """The text a reader sees, with everything else blanked and lines preserved.

    **The rule is what a reader sees speaks for the record**, and it took three
    goes to state it that widely. Fenced blocks were blanked first, because an
    example is not a declaration. Then nesting, because a four-backtick block
    holds three-backtick ones. Then *comments* — and that one was a hole, not a
    refinement: an ADR opens with `<!-- SPDX… -->`, so hidden text was already
    idiomatic in every file. A record could be born carrying `**Status:**
    Proposed` in a comment while its visible header said `Accepted`, or hidden
    `## Decision` headings that took the frozen spans off the real prose, or a
    hidden `**Supersedes:**` that excused an edit to a record already in the
    tree. All three render invisibly.

    Blanked rather than removed so line numbers still line up with a diff, and
    an unterminated fence *or* comment is a fault: a construct that swallows the
    rest of the file must not decide quietly what the file says.
    """
    kept: list[str] = []
    fence: str | None = None
    in_comment = False
    for line in lines_of(text):
        if fence is not None:
            found = CODE_FENCE.match(line)
            if found and found.group(1)[0] == fence[0] and len(found.group(1)) >= len(fence):
                fence = None
            kept.append("")
            continue
        if in_comment:
            after = line.split(COMMENT_CLOSE, 1)
            in_comment = len(after) == 1
            kept.append("")
            continue
        found = CODE_FENCE.match(line)
        if found:
            fence = found.group(1)
            kept.append("")
            continue
        # Whole comments first; whatever `<!--` survives that opens one which
        # runs on to a later line. Removed rather than replaced by a space: a
        # renderer joins what a comment separates, so `Acc<!-- -->epted` reads
        # as one word to everyone except, until this line changed, the gate.
        visible = COMMENT_SPAN.sub("", line)
        head, opened, _ = visible.partition(COMMENT_OPEN)
        in_comment = bool(opened)
        kept.append(head if opened else visible)
    if fence is not None:
        raise CannotAnswer(
            f"{path or 'document'}: a code fence is opened and never closed; "
            "the gate cannot tell which lines are prose"
        )
    if in_comment:
        raise CannotAnswer(
            f"{path or 'document'}: an HTML comment is opened and never closed; "
            "the gate cannot tell which lines a reader sees"
        )
    return kept


def visible_prose(text: str, path: str = "") -> str:
    """The same, as one string, for callers that want to search rather than count.

    Every caller used to take this and re-split it, and the round trip lost a
    trailing blank line: `"a\n\n"` joined to `"a\n"` counts one line where git
    counts two, so the last frozen section ended one line early for any record
    whose final line renders blank. The list is what the line numbers come from.
    """
    return "\n".join(visible_lines(text, path))

