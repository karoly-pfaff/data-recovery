#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for `tools/loopdev/loop_device.py`'s teardown and its options.

The module talks to `losetup`, so most of it can only run on the workbench —
but two things about it are pure control flow, and one of them is the whole of
story-0603's first acceptance criterion: that the pass detaches its devices
*even when a check fails*. A `losetup -d` that runs only on the way out of a
green run is the leak the wsl-bench skill documents, and until now nothing but
a green transcript said otherwise.

The commands are intercepted rather than run, so these pass on any platform.

Run by ctest as `LoopdevUnitTests`; `python3 -m unittest` from the repository
root works too.
"""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools" / "loopdev"))

import loop_device  # noqa: E402


class PatchedRun(unittest.TestCase):
    """Every test here replaces `loop_device._run`; this puts it back.

    Written once. The three hand-rolled copies it replaces were not identical:
    one read the attribute *after* assigning to it, so it restored the fake and
    the cleanup could not fail — the shape this whole story exists to refuse.
    """

    def patch(self, answer):
        original = loop_device._run
        self.addCleanup(setattr, loop_device, "_run", original)
        loop_device._run = answer
        return answer


class FakeLosetup:
    """Stands in for `loop_device._run`, recording what it was asked."""

    def __init__(self, device: str = "/dev/loop9") -> None:
        self.commands: list[list[str]] = []
        self.device = device

    def __call__(self, command: list[str]) -> str:
        self.commands.append(command)
        return self.device

    def detached(self) -> list[str]:
        return [c[-1] for c in self.commands if "-d" in c]


class TeardownTest(PatchedRun):
    def setUp(self):
        self.losetup = self.patch(FakeLosetup())

    def test_a_device_is_detached_on_the_way_out(self):
        with loop_device.attached(Path("disk.img")) as device:
            self.assertEqual(device, "/dev/loop9")
        self.assertEqual(self.losetup.detached(), ["/dev/loop9"])

    # The criterion the story's first acceptance line makes: a failed check
    # must not leave a device behind for the next session.
    def test_a_device_is_detached_when_the_body_raises(self):
        with self.assertRaises(RuntimeError):
            with loop_device.attached(Path("disk.img")):
                raise RuntimeError("a check blew up")
        self.assertEqual(self.losetup.detached(), ["/dev/loop9"])

    def test_an_attach_that_failed_does_not_run_the_body(self):
        def refuses(command: list[str]) -> str:
            raise loop_device.LoopError("no free loop devices")

        self.patch(refuses)
        with self.assertRaises(loop_device.LoopError):
            with loop_device.attached(Path("disk.img")):
                self.fail("the body must not run when the attach failed")


class AttachOptionsTest(PatchedRun):
    """Each option has to reach `losetup`, or the check resting on it is blind."""

    def setUp(self):
        self.losetup = self.patch(FakeLosetup())

    def command(self) -> list[str]:
        return self.losetup.commands[-1]

    def test_a_plain_attachment_asks_for_nothing_extra(self):
        loop_device.attach(Path("disk.img"))
        self.assertEqual(self.command(), ["losetup", "--show", "-f", "disk.img"])

    def test_partition_scan_reaches_losetup(self):
        loop_device.attach(Path("disk.img"), partition_scan=True)
        self.assertIn("-P", self.command())

    def test_read_only_reaches_losetup(self):
        loop_device.attach(Path("disk.img"), read_only=True)
        self.assertIn("-r", self.command())

    def test_the_sector_size_reaches_losetup_with_its_value(self):
        loop_device.attach(Path("disk.img"), sector_size=4096)
        self.assertIn("--sector-size", self.command())
        self.assertIn("4096", self.command())

    def test_the_backing_file_is_always_last(self):
        loop_device.attach(Path("disk.img"), partition_scan=True, read_only=True, sector_size=4096)
        self.assertEqual(self.command()[-1], "disk.img")


class ReadOnlyQueryTest(PatchedRun):
    def answer(self, value: str) -> bool:
        self.patch(lambda command: value)
        return loop_device.is_read_only("/dev/loop9")

    def test_one_means_the_kernel_refuses_writes(self):
        self.assertTrue(self.answer("1"))

    def test_zero_means_it_does_not(self):
        self.assertFalse(self.answer("0"))


if __name__ == "__main__":
    unittest.main()
