// SPDX-License-Identifier: GPL-3.0-or-later
// Internal. The seam between the two halves of runlist decoding: reading one
// run out of the attribute bytes (RunlistRun.cpp) and walking the list to place
// runs in cluster space (Runlist.cpp). Not a public interface.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Result.hpp"

namespace revenant::fs::ntfs {

constexpr std::size_t kBitsPerByte = 8;

// One run as it sits in the bytes: still unsigned, still relative to the
// previous run's start.
struct RawRun {
	std::uint64_t lengthClusters;
	std::uint64_t rawOffset; // meaningless when offsetWidth is 0 (sparse)
	std::size_t offsetWidth;
	std::size_t encodedSize; // header byte plus both fields
};

// Reads the run beginning at `cursor`, which the caller has already checked to
// be inside `bytes` and not the end marker.
[[nodiscard]] Result<RawRun> readRawRun(std::span<const std::byte> bytes, std::size_t cursor);

} // namespace revenant::fs::ntfs
