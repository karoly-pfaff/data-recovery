// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/Utf16Name.hpp"
#include "revenant/volume/Gpt.hpp"
#include "volume/GptLayout.hpp"

namespace revenant::volume {

namespace {

constexpr std::size_t kCodeUnitBytes = 2;

// The inclusive sector range a slot claims.
struct LbaRange {
	std::uint64_t firstLba = 0;
	std::uint64_t lastLba = 0;
};

[[nodiscard]] std::array<std::byte, kGuidBytes> guidFrom(std::span<const std::byte> raw) {
	std::array<std::byte, kGuidBytes> guid{};
	std::ranges::copy(raw, guid.begin());
	return guid;
}

// The code units before the first NUL. The field is zero-padded to 36 units, and
// handing the padding to the decoder would come back as the name followed by
// thirty escaped zeros — a lossless encoding of nothing. Trimming first is what
// makes `lossless` mean "the name survived" rather than "the padding did".
[[nodiscard]] std::span<const std::byte> nameUnitsOf(std::span<const std::byte> field) {
	for (std::size_t at = 0; at + kCodeUnitBytes <= field.size(); at += kCodeUnitBytes) {
		const auto unit = field.subspan(at, kCodeUnitBytes);
		if (unit.front() == std::byte{0} && unit.back() == std::byte{0}) {
			return field.first(at);
		}
	}
	return field;
}

// The range, checked as a range. An unused slot is sixteen zero bytes and two
// zero LBAs, which passes: it describes nothing rather than describing something
// impossible.
[[nodiscard]] Result<LbaRange> rangeOf(const ByteReader& reader) {
	return reader.readLe<std::uint64_t>(kEntryFirstLbaOffset).andThen([&](std::uint64_t first) {
		return reader.readLe<std::uint64_t>(kEntryLastLbaOffset)
			.andThen([first](std::uint64_t last) {
				if (last < first) {
					return Result<LbaRange>(
						Error{.code = ErrorCode::kInvalidArgument, .offset = kEntryLastLbaOffset});
				}
				return Result<LbaRange>(LbaRange{.firstLba = first, .lastLba = last});
			});
	});
}

[[nodiscard]] Result<GptEntry> entryWith(const ByteReader& reader, const LbaRange& range) {
	return reader.bytes(kTypeGuidOffset, kGuidBytes).andThen([&](std::span<const std::byte> guid) {
		return reader.bytes(kEntryNameOffset, kEntryNameBytes)
			.map([&](std::span<const std::byte> field) {
				const auto decoded = decodeUtf16Name(nameUnitsOf(field));
				return GptEntry{
					.typeGuid = guidFrom(guid),
					.firstLba = range.firstLba,
					.lastLba = range.lastLba,
					.name = decoded.utf8,
					.nameIsExact = decoded.lossless};
			});
	});
}

} // namespace

Result<GptEntry> parseGptEntry(std::span<const std::byte> slot) {
	if (slot.size() < kGptEntryBytes) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = slot.size()};
	}
	const ByteReader reader{slot.first(kGptEntryBytes)};
	return rangeOf(reader).andThen([&](const LbaRange& range) { return entryWith(reader, range); });
}

bool isUnusedEntry(const GptEntry& entry) noexcept {
	return std::ranges::all_of(entry.typeGuid, [](std::byte value) {
		return value == std::byte{0};
	});
}

} // namespace revenant::volume
