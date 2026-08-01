#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Does the loop-device pass's own test suite hold it to saying red?

story-0603's first full run came back green on every check, which is the state
in which a comparison that never compared anything is indistinguishable from
one that compared everything. `LoopdevUnitTests` is the answer, and this is what
says the answer is worth having: every guard in the judging half is broken here,
one at a time, in a copy of the tree, and the suite is required to go red.

A mutation whose site is no longer in the code counts as **undetected**. A list
that quietly stopped matching what it attacks would report the same green as one
that caught everything — it did exactly that twice while this story was written,
which is why the rule is here rather than in a reviewer's head.

Manual, like the pass itself, and for the same reason: it edits and re-runs the
tree. Run it after changing anything under `tools/loopdev/`:

    python3 tools/loopdev/mutate.py

It exits with the number of mutations that went undetected, so 0 is the clean
result the story records.
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
MUT = Path(tempfile.mkdtemp(prefix="revenant-mutate-"))

MUTATIONS = [
    # --- identity.py: what a single answer must look like -------------------
    ("identity.py", "listing identity never compared",
     "if image != device:", "if False:"),
    ("identity.py", "listing heading guard removed",
     "if not any(heading in line for line in device):", "if False:"),
    ("identity.py", "listing count guard removed",
     "if found != partitions:", "if False:"),
    ("identity.py", "length parser reads nothing",
     "if digits.isdigit():", "if False:"),
    ("identity.py", "unreadable entries counted as zero",
     "        if digits.isdigit():\n            lengths.append(int(digits))",
     "        lengths.append(int(digits) if digits.isdigit() else 0)"),
    ("identity.py", "kernel lengths never compared",
     "if ours != kernel:", "if False:"),
    ("identity.py", "kernel vacuity guard removed",
     "if not ours or not kernel:", "if False:"),
    ("identity.py", "backup-header note never required",
     "if not any(BACKUP_HEADER_NOTE in line for line in listing):", "if False:"),
    ("identity.py", "refusal exit status ignored",
     'problems = [] if status != 0 else ["the run exited 0"]', "problems = []"),
    ("identity.py", "refusal sentence never required",
     "if PERMISSION_SENTENCE not in text:", "if False:"),
    ("identity.py", "bare errno tolerated",
     "for bare in BARE_ERRNO if bare in text", "for bare in ()"),
    ("identity.py", "a changed source is tolerated", "elif was != now:", "elif False:"),
    ("identity.py", "a zero-byte source is tolerated", "elif was.size == 0:", "elif False:"),
    ("identity.py", "a one-sided digest is tolerated",
     "if was is None or now is None:", "if False:"),
    ("identity.py", "digesting nothing at all is tolerated",
     '    if not before:\n        return ["no source was digested, so nothing was ever watched"]',
     "    if False:\n        pass"),
    ("identity.py", "a malformed manifest is swallowed",
     'return [f"a manifest is not valid JSON: {broken}"]', "return []"),
    ("identity.py", "tree vacuity guard removed", "if not image:", "if False:"),
    ("identity.py", "tree differences swallowed",
     "differing = [name for name in names if image.get(name) != device.get(name)]",
     "differing = []"),
    ("identity.py", "manifest path check disabled",
     "if document.get(member) != expected[member]", "if False"),
    ("identity.py", "manifest image-side paths unchecked",
     '    problems += _path_problems("image run", documents[0], expected_image)\n', "\n"),
    ("identity.py", "manifest required-members guard removed",
     "missing = [member for member in REQUIRED_MEMBERS if member not in document]",
     "missing = []"),
    ("identity.py", "manifest artifacts guard removed",
     'if not document["artifacts"]:', "if False:"),
    ("identity.py", "missing manifest tolerated",
     "absent = [path for path in (image, device) if not path.is_file()]", "absent = []"),
    # --- runs.py: the one producer with a rule in it ------------------------
    ("runs.py", "tree exclusion ignored",
     "if not path.is_file() or (excluding and excluding in relative.parts):",
     "if not path.is_file():"),
    # --- checks.py: the preconditions and the scope -------------------------
    ("checks.py", "4Kn precondition never checked",
     "    if measured != FOUR_KN_SECTOR:", "    if False:"),
    ("checks.py", "read-only precondition never checked",
     "    if not refuses_writes:", "    if False:"),
    ("checks.py", "refusal precondition never checked",
     "    if not was_refused:", "    if False:"),
    # --- ledger.py: the verdict itself --------------------------------------
    ("ledger.py", "a failing check stops counting",
     "        if problems:\n            self.failures += 1", "        pass"),
    ("ledger.py", "an inconclusive check stops costing a failure",
     '        self.failures += 1\n        report("INCONCLUSIVE", name, [why])',
     '        report("INCONCLUSIVE", name, [why])'),
    ("ledger.py", "inconclusive reported as a pass",
     'report("INCONCLUSIVE", name, [why])', 'report("PASS", name, [why])'),
    ("ledger.py", "a failed scope audit stops counting",
     "        problems = self.scope_problems()\n"
     "        if problems:\n            self.failures += 1",
     "        problems = self.scope_problems()"),
    ("ledger.py", "an inconclusive check is not recorded as having run",
     "    def inconclusive(self, name: str, why: str) -> None:\n"
     "        self._recorded.append(name)",
     "    def inconclusive(self, name: str, why: str) -> None:"),
    ("ledger.py", "never-ran detection removed",
     '("never ran", [n for n in expected if n not in recorded]),', '("never ran", []),'),
    ("ledger.py", "ran-twice detection removed",
     '("ran more than once", sorted({n for n in recorded if recorded.count(n) > 1})),',
     '("ran more than once", []),'),
    ("ledger.py", "unexpected-verdict detection removed",
     '("was never expected", [n for n in recorded if n not in expected]),',
     '("was never expected", []),'),
]


def run_suite(directory: Path) -> tuple[int, list[str], int]:
    """The suite's exit status, the tests that failed, and how many ran."""
    finished = subprocess.run(
        [
            sys.executable,
            "-m",
            "unittest",
            "discover",
            "--start-directory",
            str(directory),
            "--verbose",
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    output = finished.stdout + finished.stderr
    failed = sorted(set(re.findall(r"^(?:FAIL|ERROR): (\w+)", output, re.MULTILINE)))
    ran = re.search(r"^Ran (\d+) tests?", output, re.MULTILINE)
    return finished.returncode, failed, int(ran.group(1)) if ran else 0


# What the untouched suite collects. A mutation that changes this number broke
# the import, and unittest reports that as an ERROR just as loudly as a real
# assertion failure — a red that says nothing about the suite's discrimination.
BASELINE = run_suite(REPO / "tests/unit/loopdev")[2]

undetected = 0
for module, label, needle, replacement in MUTATIONS:
    shutil.rmtree(MUT, ignore_errors=True)
    (MUT / "tools").mkdir(parents=True)
    shutil.copytree(REPO / "tools/loopdev", MUT / "tools/loopdev")
    (MUT / "tests/unit").mkdir(parents=True)
    shutil.copytree(REPO / "tests/unit/loopdev", MUT / "tests/unit/loopdev")

    target = MUT / "tools/loopdev" / module
    text = target.read_text(encoding="utf-8")
    if needle not in text:
        print(f"SKIPPED  {module}: {label}: mutation site not found")
        undetected += 1
        continue
    target.write_text(text.replace(needle, replacement, 1), encoding="utf-8")

    status, failed, ran = run_suite(MUT / "tests/unit/loopdev")
    if ran != BASELINE:
        # The suite collected a different number of tests, so the mutation broke
        # the import rather than a decision. A red that proves nothing.
        print(f"BROKEN   {label}: {ran} tests collected, not {BASELINE}")
        undetected += 1
    elif status != 0 and failed:
        print(f"CAUGHT   {label}\n         by {', '.join(failed)}")
    else:
        print(f"MISSED   {label}: the suite stayed green")
        undetected += 1

shutil.rmtree(MUT, ignore_errors=True)
print(f"\n{len(MUTATIONS)} mutations, {undetected} undetected")
sys.exit(1 if undetected else 0)
