#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Turning a case's repetitions into one headline and one veto.

The median is the headline: a single fast run proves nothing on a machine with
other work on it, and a mean lets one stalled repetition speak for all of them.
The spread is the veto — how far the repetitions disagreed, as a fraction of
the median — and the regression gate refuses to call a change a regression that
the baseline's own spread already covers.
"""
from __future__ import annotations

import statistics
from dataclasses import dataclass
from typing import Sequence


@dataclass(frozen=True)
class Samples:
    """What a case's repetitions said, in seconds."""

    median: float
    minimum: float
    maximum: float
    spread: float


def _spread(minimum: float, maximum: float, median: float) -> float:
    """The disagreement between repetitions, as a fraction of the median.

    A median of zero was measured by a clock that could not see the run, so
    there is no fraction to take.
    """
    if median <= 0.0:
        return 0.0
    return (maximum - minimum) / median


def summarize(seconds: Sequence[float]) -> Samples:
    """The statistics for one case. No repetitions is refused, not averaged."""
    if not seconds:
        raise ValueError("a benchmark with no repetitions measured nothing")
    median = statistics.median(seconds)
    return Samples(
        median=median,
        minimum=min(seconds),
        maximum=max(seconds),
        spread=_spread(min(seconds), max(seconds), median),
    )
