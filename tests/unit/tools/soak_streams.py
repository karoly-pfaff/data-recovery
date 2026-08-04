#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The soak generator's memory must not follow the image it is asked for.

story-0606 needs a fixture seventeen times larger than the bench's RAM, which
is possible only if the generator holds nothing proportional to the size. That
is a claim no unit test can make from inside the process, and the existing
`carve` verb already breaks it — `buildCarveCorpus` reserves the whole image
before writing a byte. So this asks the operating system the same question the
benchmarks ask: the child's peak resident set, measured from outside, over two
sizes sixteen times apart.
"""
from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / "tools"))

from perf.peakmemory import Watch  # noqa: E402  (path set above)

_MIB = 1 << 20
# Sixteen times apart, which a buffering generator cannot hide behind: it would
# have to hold 64 MiB against a process that idles near 10, so the growth would
# be hundreds of percent against a 10% allowance. Larger sizes make the gap more
# lopsided still but write more of the runner's disk on every ctest run, and this
# runs on both platforms in three CI jobs.
SMALL_BYTES = 4 * _MIB
LARGE_BYTES = 64 * _MIB
PLANTS = 4

# The perf gate's own peak-RSS tolerance (docs/performance/benchmarks.md): what
# two runs of the same binary may differ by before the difference is real. A
# generator that buffered its image would exceed it by two orders of magnitude.
TOLERANCE = 0.10


def peak_bytes(generator: str, size: int, into: pathlib.Path) -> int:
    """Peak resident bytes of one generator run, as the OS reports it."""
    image = into / f"soak-{size}.img"
    process = subprocess.Popen(
        [generator, "soak", str(image), str(size), str(PLANTS)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    exit_code, peak = Watch(process).reap()
    if exit_code != 0:
        raise SystemExit(f"generator failed with exit code {exit_code} at {size} bytes")
    image.unlink(missing_ok=True)
    pathlib.Path(str(image) + ".plan").unlink(missing_ok=True)
    return peak


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        raise SystemExit("usage: soak_streams.py <path-to-revenant-imagegen>")
    with tempfile.TemporaryDirectory() as work:
        into = pathlib.Path(work)
        small = peak_bytes(argv[1], SMALL_BYTES, into)
        large = peak_bytes(argv[1], LARGE_BYTES, into)
    growth = (large - small) / small
    print(
        f"peak RSS: {small} bytes at {SMALL_BYTES}, {large} bytes at {LARGE_BYTES}"
        f" — growth {growth:.1%} over a {LARGE_BYTES // SMALL_BYTES}x larger image"
    )
    if growth > TOLERANCE:
        print(
            f"FAIL: peak memory grew {growth:.1%}, past the {TOLERANCE:.0%} tolerance;"
            " the soak generator is holding something proportional to the image"
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
