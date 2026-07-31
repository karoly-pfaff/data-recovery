// SPDX-License-Identifier: GPL-3.0-or-later
#include "PdfCarver.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

#include "HeadMatch.hpp"
#include "PdfTrailer.hpp"
#include "revenant/carve/CarveResult.hpp"
#include "revenant/carve/Signature.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

namespace {

constexpr std::array<std::byte, 5>
	kPdfHeader{std::byte{'%'}, std::byte{'P'}, std::byte{'D'}, std::byte{'F'}, std::byte{'-'}};

constexpr Signature kSignature{.magic = kPdfHeader, .offset = 0};

[[nodiscard]] CarveResult rejected() {
	return {.length = 0, .confidence = Confidence::kRejected, .extension = "pdf"};
}

// The extent is exact either way — the marker is where the file ends. What the
// resolved cross reference adds is the confidence that this is a whole PDF and
// not a header followed by a stray `%%EOF`.
[[nodiscard]] CarveResult verdictFor(const ByteReader& reader, const PdfTrailer& trailer) {
	return {
		.length = std::min(trailer.end, reader.size()),
		.confidence = trailer.crossReferenceResolves ? Confidence::kValid : Confidence::kUncertain,
		.extension = "pdf"};
}

} // namespace

std::span<const Signature> PdfCarver::signatures() const {
	return {&kSignature, 1};
}

Result<CarveResult> PdfCarver::carve(ByteReader& reader) const {
	if (!headMatches(reader, kPdfHeader)) {
		return rejected();
	}
	const auto trailer = findPdfTrailer(reader);
	if (!trailer.hasValue()) {
		return rejected();
	}
	return verdictFor(reader, trailer.value());
}

} // namespace revenant::carve
