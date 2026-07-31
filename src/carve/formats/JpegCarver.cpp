// SPDX-License-Identifier: GPL-3.0-or-later
#include "JpegCarver.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "HeadMatch.hpp"
#include "JpegMarkerWalk.hpp"
#include "revenant/carve/CarveResult.hpp"
#include "revenant/carve/Signature.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

namespace {

constexpr std::array<std::byte, 3> kSoiSignature{std::byte{0xFF}, std::byte{0xD8}, std::byte{0xFF}};
constexpr std::string_view kJpegExtension = "jpg";

CarveResult makeResult(std::uint64_t length, Confidence confidence) {
	return {.length = length, .confidence = confidence, .extension = std::string{kJpegExtension}};
}

// Maps a marker-walk outcome (story-0103's Valid/Uncertain/Rejected rules)
// to the reported verdict and extent.
CarveResult verdictFor(const JpegWalkOutcome& outcome) {
	if (outcome.reachedEoi && outcome.sawSos) {
		return makeResult(outcome.end, Confidence::kValid);
	}
	if (outcome.sawSos) {
		return makeResult(outcome.end, Confidence::kUncertain);
	}
	return makeResult(0, Confidence::kRejected);
}

constexpr Signature kJpegSignature{.magic = kSoiSignature, .offset = 0};

} // namespace

std::span<const Signature> JpegCarver::signatures() const {
	return {&kJpegSignature, 1};
}

Result<CarveResult> JpegCarver::carve(ByteReader& reader) const {
	if (!headMatches(reader, kSoiSignature)) {
		return verdictFor(JpegWalkOutcome{});
	}
	return verdictFor(walkJpegMarkers(reader));
}

} // namespace revenant::carve
