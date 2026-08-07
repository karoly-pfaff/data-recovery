# SPDX-License-Identifier: GPL-3.0-or-later
"""One half of a Python duplication fixture (story-0703).

The duplicated block is inside a function body in both files, because the gate
reports a block only when every one of its sites reaches code.
"""


def summarise_alpha(rows):
    total = 0
    kept = []
    for row in rows:
        if row is None:
            continue
        if row.get("skip"):
            continue
        value = int(row.get("value", 0))
        if value < 0:
            raise ValueError("negative")
        total += value
        kept.append(value)
    average = total / len(kept) if kept else 0
    return {"total": total, "kept": kept, "average": average}
