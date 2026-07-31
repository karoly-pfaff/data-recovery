// SPDX-License-Identifier: GPL-3.0-or-later
#include "PngCarver.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "HeadMatch.hpp"
#include "PngChunkWalk.hpp"
#include "revenant/carve/CarveResult.hpp"
#include "revenant/carve/Signature.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

namespace {

constexpr std::array<std::byte, 8> kPngSignature{
	std::byte{0x89},
	std::byte{0x50},
	std::byte{0x4E},
	std::byte{0x47},
	std::byte{0x0D},
	std::byte{0x0A},
	std::byte{0x1A},
	std::byte{0x0A}};
constexpr std::string_view kPngExtension = "png";

CarveResult makeResult(std::uint64_t length, Confidence confidence) {
	return {.length = length, .confidence = confidence, .extension = std::string{kPngExtension}};
}

// Without IHDR these bytes are not a PNG at all; with it, whether IEND was
// reached is the difference between an exact file and a trusted prefix.
CarveResult verdictFor(const PngWalkOutcome& outcome) {
	if (!outcome.sawIhdr) {
		return makeResult(0, Confidence::kRejected);
	}
	if (outcome.reachedIend) {
		return makeResult(outcome.end, Confidence::kValid);
	}
	return makeResult(outcome.end, Confidence::kUncertain);
}

constexpr Signature kSignature{.magic = kPngSignature, .offset = 0};

} // namespace

std::span<const Signature> PngCarver::signatures() const {
	return {&kSignature, 1};
}

Result<CarveResult> PngCarver::carve(ByteReader& reader) const {
	if (!headMatches(reader, kPngSignature)) {
		return verdictFor(PngWalkOutcome{});
	}
	return verdictFor(walkPngChunks(reader));
}

} // namespace revenant::carve
