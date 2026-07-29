#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for the benchmark harness in `tools/perf/`.

The benchmark *numbers* are machine-dependent and untestable. Everything around
them is not: the statistics that turn repetitions into a headline, the JSON the
regression gate reads, and the rule that a case whose subprocess failed is a
failure rather than a very fast run.

Run by ctest alongside the other gate tests; `python3 -m unittest` from the
repository root works too.
"""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools" / "perf"))

import compare_baseline  # noqa: E402
import measure  # noqa: E402
import report  # noqa: E402
import samples  # noqa: E402


class MedianTest(unittest.TestCase):
    def test_median_of_an_odd_count_is_the_middle_value(self):
        self.assertEqual(samples.summarize([3.0, 1.0, 2.0]).median, 2.0)

    def test_median_of_an_even_count_is_between_the_middle_two(self):
        self.assertEqual(samples.summarize([1.0, 2.0, 3.0, 4.0]).median, 2.5)

    def test_the_extremes_are_reported_as_measured(self):
        measured = samples.summarize([3.0, 1.0, 2.0])
        self.assertEqual((measured.minimum, measured.maximum), (1.0, 3.0))

    def test_the_spread_is_the_range_over_the_median(self):
        self.assertAlmostEqual(samples.summarize([1.0, 2.0, 3.0]).spread, 1.0)

    def test_one_sample_has_no_spread(self):
        self.assertEqual(samples.summarize([0.5]).spread, 0.0)

    # A gate reading zero repetitions would report a rate nobody measured.
    def test_no_samples_is_refused(self):
        with self.assertRaises(ValueError):
            samples.summarize([])


class ReportTest(unittest.TestCase):
    def test_the_json_carries_every_field_the_gate_reads(self):
        entry = report.entry_for(
            name="scan-throughput",
            unit="MiB/s",
            timings=samples.summarize([1.0, 2.0, 3.0]),
            peak_rss_bytes=1024,
            work_units=256.0,
            instructions=99,
        )
        for metric in compare_baseline.METRICS:
            self.assertIn(metric.key, entry)
        self.assertIn("name", entry)

    def test_the_rate_is_the_work_over_the_median(self):
        entry = report.entry_for(
            name="scan-throughput",
            unit="MiB/s",
            timings=samples.summarize([2.0]),
            peak_rss_bytes=1024,
            work_units=256.0,
            instructions=None,
        )
        self.assertEqual(entry["rate"], 128.0)

    # Absent, not faked: a machine with no valgrind measured no instructions.
    def test_an_unmeasured_instruction_count_is_absent_rather_than_zero(self):
        entry = report.entry_for(
            name="scan-throughput",
            unit="MiB/s",
            timings=samples.summarize([2.0]),
            peak_rss_bytes=1024,
            work_units=256.0,
            instructions=None,
        )
        self.assertNotIn("instructions", entry)


class MeasureTest(unittest.TestCase):
    def test_a_failing_subprocess_is_a_failure_not_a_zero_measurement(self):
        with self.assertRaises(measure.CaseFailed):
            measure.run_measured([sys.executable, "-c", "raise SystemExit(3)"])

    def test_a_run_reports_its_output_and_a_peak_memory_above_zero(self):
        measured = measure.run_measured([sys.executable, "-c", "print('regions scanned 1')"])
        self.assertIn("regions scanned 1", measured.output)
        self.assertGreater(measured.peak_rss_bytes, 0)

    def test_a_run_reports_the_time_it_took(self):
        measured = measure.run_measured([sys.executable, "-c", "pass"])
        self.assertGreater(measured.seconds, 0.0)

    def test_a_binary_that_is_not_there_is_a_failure(self):
        with self.assertRaises(measure.CaseFailed):
            measure.run_measured(["revenant-not-a-binary-at-all"])


class WorkUnitsTest(unittest.TestCase):
    SUMMARY = "[info] discovery: filesystem entries 4, carve candidates 8192, regions scanned 1"

    def test_a_labelled_count_is_read_from_the_run_summary(self):
        self.assertEqual(report.counted(self.SUMMARY, "carve candidates"), 8192.0)

    # The run says what it did; a harness that guessed would report a guess.
    def test_a_label_the_run_never_printed_is_refused(self):
        with self.assertRaises(ValueError):
            report.counted(self.SUMMARY, "wombats found")


if __name__ == "__main__":
    unittest.main()
