// SPDX-License-Identifier: GPL-3.0-or-later
#include "PngCarver.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

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

bool startsWithSignature(const ByteReader& reader) {
	const auto head = reader.bytes(0, kPngSignature.size());
	if (!head.hasValue()) {
		return false;
	}
	return std::ranges::equal(head.value(), kPngSignature);
}

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
	if (!startsWithSignature(reader)) {
		return verdictFor(PngWalkOutcome{});
	}
	return verdictFor(walkPngChunks(reader));
}

} // namespace revenant::carve
