// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "revenant/core/Result.hpp"

namespace revenant::imagegen {

// One planted file: where it starts on the device and how long it is. This is
// ground truth for a soak run's manifest — the recovered files' source extents
// are checked against these, rather than only against themselves.
struct Plant {
	std::uint64_t offset = 0;
	std::uint64_t length = 0;
};

// Every plant is a JPEG of this size: a whole number of sectors, so the filler
// on either side of one stays a function of the device offset.
inline constexpr std::size_t kSoakPlantBytes = 32768;

// Where `writeSoakImage` plants: `plantCount` files, evenly spaced and
// sector-aligned, the first at offset zero. Empty when they would not fit,
// which is the one answer a caller must not read as "plant fewer".
[[nodiscard]] std::vector<Plant> soakPlan(std::uint64_t sizeBytes, std::uint64_t plantCount);

// The path `writeSoakImage` records its plan at, for an image at `outputPath`.
[[nodiscard]] std::filesystem::path soakPlanPath(const std::filesystem::path& outputPath);

// `sizeBytes` of counter filler with `plantCount` carveable JPEGs planted in
// it; returns the bytes written. Streamed: nothing proportional to `sizeBytes`
// is ever held, which is what makes a fixture larger than memory possible at
// all. The plan lands beside the image, one `offset length` pair per line.
[[nodiscard]] Result<std::uint64_t> writeSoakImage(
	const std::filesystem::path& outputPath,
	std::uint64_t sizeBytes,
	std::uint64_t plantCount);

} // namespace revenant::imagegen
