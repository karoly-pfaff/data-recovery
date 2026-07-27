// SPDX-License-Identifier: GPL-3.0-or-later
#include "Mp4Carver.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "Mp4BoxWalk.hpp"
#include "revenant/carve/CarveResult.hpp"
#include "revenant/carve/Signature.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

namespace {

constexpr std::size_t kTypeBytes = 4;
constexpr std::size_t kFtypOffset = 4;
constexpr std::size_t kBrandOffset = 8;
constexpr std::string_view kMp4Extension = "mp4";
constexpr std::string_view kMovExtension = "mov";

constexpr std::array<std::byte, kTypeBytes> kFtypMagic{
	std::byte{'f'},
	std::byte{'t'},
	std::byte{'y'},
	std::byte{'p'}};
constexpr std::array<std::byte, kTypeBytes> kQuickTimeBrand{
	std::byte{'q'},
	std::byte{'t'},
	std::byte{' '},
	std::byte{' '}};

// The major brand sits right behind the `ftyp` header; QuickTime files carry
// `qt  ` there and deserve the extension a player will recognize.
[[nodiscard]] std::string extensionFor(const ByteReader& reader) {
	const auto brand = reader.bytes(kBrandOffset, kTypeBytes);
	if (brand.hasValue() && std::ranges::equal(brand.value(), kQuickTimeBrand)) {
		return std::string{kMovExtension};
	}
	return std::string{kMp4Extension};
}

// Without `ftyp` these bytes are not an ISO base media file at all. With it,
// a file that carries both its metadata (`moov`) and its media (`mdat`) is
// complete; anything less is a trusted prefix, not a whole file.
CarveResult verdictFor(const Mp4WalkOutcome& outcome, std::string extension) {
	if (!outcome.sawFtyp) {
		return {
			.length = 0,
			.confidence = Confidence::kRejected,
			.extension = std::move(extension)};
	}
	const auto grade =
		(outcome.sawMoov && outcome.sawMdat) ? Confidence::kValid : Confidence::kUncertain;
	return {.length = outcome.end, .confidence = grade, .extension = std::move(extension)};
}

constexpr Signature kSignature{.magic = kFtypMagic, .offset = kFtypOffset};

} // namespace

std::span<const Signature> Mp4Carver::signatures() const {
	return {&kSignature, 1};
}

Result<CarveResult> Mp4Carver::carve(ByteReader& reader) const {
	return verdictFor(walkMp4Boxes(reader), extensionFor(reader));
}

} // namespace revenant::carve
