// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/carve/Plausibility.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include "revenant/carve/CarveResult.hpp"
#include "revenant/core/Confidence.hpp"

namespace revenant::carve {

namespace {

// Floors chosen from what the formats themselves require, not from taste:
// a JPEG needs quantization and Huffman tables before any image data; an ISO
// base media file needs ftyp, moov and mdat with their headers; a TIFF needs a
// header, an IFD and a strip; a PDF needs a header, an object, an xref and a
// trailer; a ZIP needs a local header, a directory entry and an end record.
// A format this build ships no carver for gets no floor at all. Inventing a
// size limit for a format we know nothing about is exactly the arbitrary
// heuristic this filter is meant not to be.
constexpr std::uint64_t kDefaultMinimumBytes = 0;

constexpr std::array<std::pair<std::string_view, std::uint64_t>, 13> kMinimums{
	std::pair{std::string_view{"jpg"}, std::uint64_t{512}},
	std::pair{std::string_view{"png"}, std::uint64_t{128}},
	std::pair{std::string_view{"mp4"}, std::uint64_t{1024}},
	std::pair{std::string_view{"mov"}, std::uint64_t{1024}},
	std::pair{std::string_view{"cr2"}, std::uint64_t{1024}},
	std::pair{std::string_view{"nef"}, std::uint64_t{1024}},
	std::pair{std::string_view{"arw"}, std::uint64_t{1024}},
	std::pair{std::string_view{"tif"}, std::uint64_t{1024}},
	std::pair{std::string_view{"pdf"}, std::uint64_t{256}},
	std::pair{std::string_view{"zip"}, std::uint64_t{128}},
	std::pair{std::string_view{"docx"}, std::uint64_t{128}},
	std::pair{std::string_view{"xlsx"}, std::uint64_t{128}},
	std::pair{std::string_view{"pptx"}, std::uint64_t{128}}};

[[nodiscard]] bool belowFloor(const CarveResult& result) {
	return result.length < plausibleMinimumBytes(result.extension);
}

} // namespace

std::uint64_t plausibleMinimumBytes(std::string_view extension) {
	const auto found =
		std::ranges::find(kMinimums, extension, &std::pair<std::string_view, std::uint64_t>::first);
	return found == kMinimums.end() ? kDefaultMinimumBytes : found->second;
}

CarveResult applyPlausibility(CarveResult result) {
	if (result.confidence == Confidence::kRejected || !belowFloor(result)) {
		return result;
	}
	return {
		.length = 0,
		.confidence = Confidence::kRejected,
		.extension = std::move(result.extension)};
}

} // namespace revenant::carve
