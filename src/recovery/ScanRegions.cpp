// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery/ScanRegions.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/carve/SignatureScanner.hpp"

namespace revenant::recovery {

namespace {

[[nodiscard]] std::uint64_t endOf(const carve::ScanRegion& region) noexcept {
	return region.offset + region.lengthBytes;
}

// What is left of one region once everything before `cursor` is spoken for.
[[nodiscard]] carve::ScanRegion tailOf(const carve::ScanRegion& region, std::uint64_t cursor) {
	const auto start = std::max(region.offset, cursor);
	return carve::ScanRegion{.offset = start, .lengthBytes = endOf(region) - start};
}

void appendChunks(
	std::vector<carve::ScanRegion>& chunks,
	const carve::ScanRegion& region,
	std::uint64_t chunkBytes) {
	for (auto at = region.offset; at < endOf(region);) {
		const auto length = std::min(chunkBytes, endOf(region) - at);
		chunks.push_back(carve::ScanRegion{.offset = at, .lengthBytes = length});
		at += length;
	}
}

} // namespace

std::vector<carve::ScanRegion>
regionsFrom(std::span<const carve::ScanRegion> regions, std::uint64_t cursor) {
	std::vector<carve::ScanRegion> remaining;
	for (const carve::ScanRegion& region : regions) {
		if (endOf(region) > cursor) {
			remaining.push_back(tailOf(region, cursor));
		}
	}
	return remaining;
}

std::vector<carve::ScanRegion>
chunked(std::span<const carve::ScanRegion> regions, std::uint64_t chunkBytes) {
	std::vector<carve::ScanRegion> chunks;
	for (const carve::ScanRegion& region : regions) {
		appendChunks(chunks, region, chunkBytes);
	}
	return chunks;
}

} // namespace revenant::recovery
