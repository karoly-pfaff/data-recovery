#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Peak resident memory of a child process, asked of the operating system.

This is the one place in the harness that knows which OS it is on. Both of them
already track the number — the child's high-water working set — so measuring it
from outside costs nothing, which is most of why the benchmarks drive the
shipped binaries instead of calling into them.

A `Watch` is opened while the child is alive and reaped when it exits, because
that is what both platforms need: POSIX reports the peak *with* the exit status
and only for a child nothing else has waited for, and Windows keeps it on the
process object until the last handle to it closes.
"""
from __future__ import annotations

import os
import subprocess
import sys

if sys.platform == "win32":  # pragma: no cover - selected by platform
    import ctypes
    import ctypes.wintypes

    _PROCESS_QUERY_INFORMATION = 0x0400
    _PROCESS_VM_READ = 0x0010

    class _MemoryCounters(ctypes.Structure):
        _fields_ = [
            ("cb", ctypes.wintypes.DWORD),
            ("PageFaultCount", ctypes.wintypes.DWORD),
            ("PeakWorkingSetSize", ctypes.c_size_t),
            ("WorkingSetSize", ctypes.c_size_t),
            ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
            ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
            ("PagefileUsage", ctypes.c_size_t),
            ("PeakPagefileUsage", ctypes.c_size_t),
        ]

    class Watch:
        """A second handle on the child, so its counters outlive its exit."""

        def __init__(self, process: subprocess.Popen) -> None:
            self._process = process
            self._handle = ctypes.windll.kernel32.OpenProcess(
                _PROCESS_QUERY_INFORMATION | _PROCESS_VM_READ, False, process.pid
            )

        def _peak_bytes(self) -> int:
            counters = _MemoryCounters()
            counters.cb = ctypes.sizeof(counters)
            ctypes.windll.psapi.GetProcessMemoryInfo(
                self._handle, ctypes.byref(counters), counters.cb
            )
            return int(counters.PeakWorkingSetSize)

        def reap(self) -> tuple[int, int]:
            """The child's exit code and its peak resident bytes."""
            exit_code = self._process.wait()
            peak = self._peak_bytes()
            ctypes.windll.kernel32.CloseHandle(self._handle)
            return exit_code, peak

else:
    _KIBIBYTE = 1024

    class Watch:
        """Nothing to open: POSIX reports the peak with the exit status."""

        def __init__(self, process: subprocess.Popen) -> None:
            self._process = process

        def reap(self) -> tuple[int, int]:
            """The child's exit code and its peak resident bytes.

            `wait4` reports the rusage of *this* child, unlike RUSAGE_CHILDREN,
            which is a running maximum over every child already reaped and
            would let a heavy case speak for the light one measured after it.
            The `returncode` is filled in by hand because this call, not
            `Popen.wait`, is what reaped the process.
            """
            _, status, usage = os.wait4(self._process.pid, 0)
            self._process.returncode = os.waitstatus_to_exitcode(status)
            return self._process.returncode, usage.ru_maxrss * _KIBIBYTE
