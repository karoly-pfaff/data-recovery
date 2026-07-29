// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The fixed, synthetic inputs the benchmarks run against. Built in memory and
// identical every run, so two measurements differ because the code did — which
// is the only reason a benchmark is worth keeping.

#include <cstddef>
#include <vector>

namespace revenant::perf {

// How much of a device each scanning benchmark chews through. Big enough that
// the window loop runs many times; small enough that five repetitions finish
// while someone is watching.
inline constexpr std::size_t kScanImageBytes = std::size_t{32} << 20U;

// One JPEG header per this many bytes. Real media is nowhere near this dense;
// the point is to exercise the *hit* path often enough to matter, because a
// scanner that is only ever measured on misses is measured on half its job.
inline constexpr std::size_t kHeaderEveryBytes = std::size_t{256} << 10U;

// `kScanImageBytes` of non-repeating filler with a real, carvable JPEG planted
// every `kHeaderEveryBytes`.
[[nodiscard]] const std::vector<std::byte>& scanImage();

// The story-0118 NTFS fixture volume, built once.
[[nodiscard]] const std::vector<std::byte>& ntfsVolume();

// The four-filesystem whole disk from story-0405, built once.
[[nodiscard]] const std::vector<std::byte>& wholeDisk();

// One valid JPEG and one that is truncated part-way through its scan — the two
// sides of validation, since rejecting cheaply matters as much as accepting.
[[nodiscard]] const std::vector<std::byte>& validJpeg();
[[nodiscard]] const std::vector<std::byte>& truncatedJpeg();

} // namespace revenant::perf
