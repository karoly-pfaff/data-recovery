// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/SoakImage.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "imagegen/FixtureJpeg.hpp"
#include "imagegen/ImageFile.hpp"
#include "imagegen/PatternWriter.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::imagegen {

namespace {

constexpr std::string_view kPlanSuffix = ".plan";

// The gap between plants, or zero when `plantCount` of them do not fit in
// `sizeBytes`. Rounded down to a sector, so every plant starts on one.
[[nodiscard]] std::uint64_t plantStride(std::uint64_t sizeBytes, std::uint64_t plantCount) {
	if (plantCount == 0) {
		return 0;
	}
	const std::uint64_t stride = (sizeBytes / plantCount) / kSectorBytes * kSectorBytes;
	return stride < kSoakPlantBytes ? 0 : stride;
}

// Carries the stream from `from` to the end of `plant`: the filler that reaches
// it, then the plant itself, stamped with its own offset. Returns the device
// offset just past the plant.
//
// Stamped because the extractor deduplicates by content hash: 256 copies of one
// JPEG would recover as one file and 255 duplicates, and every artifact in the
// manifest would carry the same SHA-256 — a comparison that cannot see a hash
// attached to the wrong file.
std::uint64_t fillUpToAndWritePlant(
	std::ostream& stream,
	std::span<std::byte> jpeg,
	std::uint64_t from,
	const Plant& plant) {
	const auto reached = writeFiller(stream, from, plant.offset, Pattern::kCounter);
	if (reached < plant.offset) {
		return reached; // the stream failed short of the plant; nothing was planted
	}
	stampJpegPayload(jpeg, plant.offset);
	return reached + writeBytesTo(stream, jpeg);
}

// Every plant, with the filler between them; returns the offset past the last.
// One JPEG buffer is built, restamped and written many times: that, and filler a
// sector at a time, are the whole of this generator's memory.
[[nodiscard]] std::uint64_t writePlants(std::ostream& stream, std::span<const Plant> plan) {
	auto jpeg = fixtureJpeg(kSoakPlantBytes);
	std::uint64_t at = 0;
	for (const Plant& plant : plan) {
		at = fillUpToAndWritePlant(stream, jpeg, at, plant);
	}
	return at;
}

[[nodiscard]] Result<std::uint64_t> writeImageBody(
	const std::filesystem::path& outputPath,
	std::uint64_t sizeBytes,
	std::span<const Plant> plan) {
	return writeImageFile(outputPath, [sizeBytes, plan](std::ostream& stream) {
		const auto planted = writePlants(stream, plan);
		return writeFiller(stream, planted, sizeBytes, Pattern::kCounter);
	});
}

// The plan as its file spells it: one `offset length` pair per line.
[[nodiscard]] std::string planText(std::span<const Plant> plan) {
	std::string text;
	for (const Plant& plant : plan) {
		text += std::to_string(plant.offset) + " " + std::to_string(plant.length) + "\n";
	}
	return text;
}

[[nodiscard]] Result<std::uint64_t>
recordPlan(const std::filesystem::path& outputPath, std::span<const Plant> plan) {
	const auto text = planText(plan);
	return writeImageBytes(soakPlanPath(outputPath), std::as_bytes(std::span{text}));
}

} // namespace

std::vector<Plant> soakPlan(std::uint64_t sizeBytes, std::uint64_t plantCount) {
	// `stride * plantCount` cannot overflow: stride is `sizeBytes / plantCount`
	// rounded down, so the product is at most `sizeBytes`. A stride of zero —
	// the plants do not fit — makes the loop body unreachable.
	const auto stride = plantStride(sizeBytes, plantCount);
	std::vector<Plant> plan;
	for (std::uint64_t at = 0; at < stride * plantCount; at += stride) {
		plan.push_back(Plant{.offset = at, .length = kSoakPlantBytes});
	}
	return plan;
}

std::filesystem::path soakPlanPath(const std::filesystem::path& outputPath) {
	auto path = outputPath;
	path += kPlanSuffix;
	return path;
}

Result<std::uint64_t> writeSoakImage(
	const std::filesystem::path& outputPath,
	std::uint64_t sizeBytes,
	std::uint64_t plantCount) {
	const auto plan = soakPlan(sizeBytes, plantCount);
	if (plan.empty()) {
		return Error{.code = ErrorCode::kInvalidArgument};
	}
	const auto written = writeImageBody(outputPath, sizeBytes, plan);
	if (!written.hasValue()) {
		return written;
	}
	return recordPlan(outputPath, plan).map([&](std::uint64_t) { return written.value(); });
}

} // namespace revenant::imagegen
