// SPDX-License-Identifier: GPL-3.0-or-later
#include "RawCarver.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <span>
#include <string>

#include "TiffEntry.hpp"
#include "TiffIfdWalk.hpp"
#include "revenant/carve/CarveResult.hpp"
#include "revenant/carve/Signature.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

namespace {

constexpr std::size_t kCanonMarkerOffset = 8;
constexpr std::size_t kMakeSampleBytes = 8;

constexpr std::array<std::byte, 4> kLittleEndianMagic{
	std::byte{0x49},
	std::byte{0x49},
	std::byte{0x2A},
	std::byte{0x00}};
constexpr std::array<std::byte, 4> kBigEndianMagic{
	std::byte{0x4D},
	std::byte{0x4D},
	std::byte{0x00},
	std::byte{0x2A}};
constexpr std::array<std::byte, 2> kCanonMarker{std::byte{'C'}, std::byte{'R'}};

constexpr std::array<Signature, 2> kSignatures{
	Signature{.magic = kLittleEndianMagic, .offset = 0},
	Signature{.magic = kBigEndianMagic, .offset = 0}};

[[nodiscard]] Result<bool> orderFromMagic(std::span<const std::byte> head) {
	if (std::ranges::equal(head, kBigEndianMagic)) {
		return true;
	}
	if (std::ranges::equal(head, kLittleEndianMagic)) {
		return false;
	}
	return Error{.code = ErrorCode::kNotFound, .offset = 0};
}

// The header's first four bytes are the byte order; nothing else in the file
// can be read without them.
[[nodiscard]] Result<bool> byteOrderOf(const ByteReader& reader) {
	return reader.bytes(0, kLittleEndianMagic.size()).andThen(orderFromMagic);
}

[[nodiscard]] bool hasCanonMarker(const ByteReader& reader) {
	const auto marker = reader.bytes(kCanonMarkerOffset, kCanonMarker.size());
	return marker.hasValue() && std::ranges::equal(marker.value(), kCanonMarker);
}

// The first bytes of the Make tag's string, which name the camera vendor.
[[nodiscard]] std::string makeText(const TiffContext& tiff, const TiffEntry& make) {
	const auto wanted = std::min<std::size_t>(make.count, kMakeSampleBytes);
	const auto raw = tiff.reader.bytes(make.valueOffset, wanted);
	if (make.tag == 0 || !raw.hasValue()) {
		return {};
	}
	std::string text;
	std::ranges::transform(raw.value(), std::back_inserter(text), [](std::byte value) {
		return static_cast<char>(value);
	});
	return text;
}

[[nodiscard]] std::string extensionFor(const TiffContext& tiff, const TiffWalkOutcome& outcome) {
	if (hasCanonMarker(tiff.reader)) {
		return "cr2";
	}
	const auto make = makeText(tiff, outcome.make);
	if (make.starts_with("NIKON")) {
		return "nef";
	}
	return make.starts_with("SONY") ? "arw" : "tif";
}

// A TIFF header with no readable IFD leaves nothing to recover, so it is
// refused rather than reported as a zero-length file.
[[nodiscard]] CarveResult verdictFor(const TiffContext& tiff, const TiffWalkOutcome& outcome) {
	if (!outcome.sawIfd) {
		return {.length = 0, .confidence = Confidence::kRejected, .extension = "tif"};
	}
	const auto whole = outcome.sawImageData && outcome.withinBounds && outcome.chainComplete;
	return {
		.length = std::min(outcome.end, tiff.reader.size()),
		.confidence = whole ? Confidence::kValid : Confidence::kUncertain,
		.extension = extensionFor(tiff, outcome)};
}

} // namespace

std::span<const Signature> RawCarver::signatures() const {
	return kSignatures;
}

Result<CarveResult> RawCarver::carve(ByteReader& reader) const {
	const auto bigEndian = byteOrderOf(reader);
	if (!bigEndian.hasValue()) {
		return CarveResult{.length = 0, .confidence = Confidence::kRejected, .extension = "tif"};
	}
	const TiffContext tiff{.reader = reader, .bigEndian = bigEndian.value()};
	return verdictFor(tiff, walkTiffIfds(tiff));
}

} // namespace revenant::carve
