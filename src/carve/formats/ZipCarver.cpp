// SPDX-License-Identifier: GPL-3.0-or-later
#include "ZipCarver.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "AsciiText.hpp"
#include "HeadMatch.hpp"
#include "ZipEndRecord.hpp"
#include "revenant/carve/CarveResult.hpp"
#include "revenant/carve/Signature.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

namespace {

constexpr std::size_t kNameSampleBytes = 64;

constexpr std::array<std::byte, 4> kLocalHeaderSignature{
	std::byte{0x50},
	std::byte{0x4B},
	std::byte{0x03},
	std::byte{0x04}};

constexpr Signature kSignature{.magic = kLocalHeaderSignature, .offset = 0};

// Office formats are ZIP archives whose first-level directories name them.
constexpr std::array<std::pair<std::string_view, std::string_view>, 3> kOfficePrefixes{
	std::pair{std::string_view{"word/"}, std::string_view{"docx"}},
	std::pair{std::string_view{"xl/"}, std::string_view{"xlsx"}},
	std::pair{std::string_view{"ppt/"}, std::string_view{"pptx"}}};

// The opening bytes of the central directory, read as text. One sample is
// enough: the directory lists entry names back to back, so an Office archive's
// telltale prefix appears within the first entries.
[[nodiscard]] std::string directorySample(const ByteReader& reader, const ZipEndRecord& record) {
	const auto raw = reader.bytes(record.centralDirectoryOffset, kNameSampleBytes);
	return raw.hasValue() ? asciiText(raw.value()) : std::string{};
}

// Searched by walking rather than through an iterator: libstdc++ makes
// `std::array`'s iterator a raw pointer and the MSVC STL makes it a class, so no
// single spelling of the `auto` holding one satisfies both toolchains' lint.
[[nodiscard]] std::string extensionFor(const ByteReader& reader, const ZipEndRecord& record) {
	const auto sample = directorySample(reader, record);
	for (const auto& [prefix, extension] : kOfficePrefixes) {
		if (sample.find(prefix) != std::string::npos) {
			return std::string{extension};
		}
	}
	return "zip";
}

[[nodiscard]] CarveResult rejected() {
	return {.length = 0, .confidence = Confidence::kRejected, .extension = "zip"};
}

// An EOCD whose directory arithmetic holds ends the archive exactly; one that
// merely looks like an EOCD bounds it, and says so.
[[nodiscard]] CarveResult verdictFor(const ByteReader& reader, const ZipEndRecord& record) {
	const auto whole = record.centralDirectoryChecksOut && record.end <= reader.size();
	return {
		.length = std::min(record.end, reader.size()),
		.confidence = whole ? Confidence::kValid : Confidence::kUncertain,
		.extension = extensionFor(reader, record)};
}

} // namespace

std::span<const Signature> ZipCarver::signatures() const {
	return {&kSignature, 1};
}

Result<CarveResult> ZipCarver::carve(ByteReader& reader) const {
	if (!headMatches(reader, kLocalHeaderSignature)) {
		return rejected();
	}
	const auto record = findZipEndRecord(reader);
	if (!record.hasValue()) {
		return rejected();
	}
	return verdictFor(reader, record.value());
}

} // namespace revenant::carve
