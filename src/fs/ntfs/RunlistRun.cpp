// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>

#include "RunlistInternal.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::ntfs {

namespace {

constexpr std::uint8_t kNibbleMask = 0x0FU;
constexpr std::uint8_t kNibbleBits = 4U;
constexpr std::size_t kMaxFieldWidth = 8;

// The two field widths a run header byte encodes, in bytes.
struct RunHeader {
	std::size_t lengthWidth;
	std::size_t offsetWidth;
};

[[nodiscard]] Result<RunHeader> readRunHeader(const ByteReader& reader, std::size_t cursor) {
	const auto raw = reader.readLe<std::uint8_t>(cursor);
	if (!raw.hasValue()) {
		return raw.error();
	}
	const RunHeader widths{
		.lengthWidth = static_cast<std::size_t>(raw.value() & kNibbleMask),
		.offsetWidth = static_cast<std::size_t>((raw.value() >> kNibbleBits) & kNibbleMask)};
	if (widths.lengthWidth == 0 || widths.lengthWidth > kMaxFieldWidth ||
		widths.offsetWidth > kMaxFieldWidth) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = cursor};
	}
	return widths;
}

// Little-endian assembly of a 1..8 byte field. An empty field reads as 0, which
// is what a zero-width (sparse) offset means.
[[nodiscard]] std::uint64_t readLeField(std::span<const std::byte> field) noexcept {
	std::uint64_t value = 0;
	for (const std::byte part : std::views::reverse(field)) {
		value = (value << kBitsPerByte) | std::to_integer<std::uint64_t>(part);
	}
	return value;
}

// The length and offset fields together, bounds-checked as one range.
[[nodiscard]] Result<std::span<const std::byte>>
readRunFields(std::span<const std::byte> bytes, std::size_t cursor, const RunHeader& widths) {
	const ByteReader reader{bytes};
	return reader.bytes(cursor + 1, widths.lengthWidth + widths.offsetWidth);
}

[[nodiscard]] Result<RawRun>
makeRawRun(std::span<const std::byte> fields, const RunHeader& widths, std::uint64_t offset) {
	const auto lengthClusters = readLeField(fields.first(widths.lengthWidth));
	if (lengthClusters == 0) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = offset};
	}
	return RawRun{
		.lengthClusters = lengthClusters,
		.rawOffset = readLeField(fields.subspan(widths.lengthWidth)),
		.offsetWidth = widths.offsetWidth,
		.encodedSize = 1 + widths.lengthWidth + widths.offsetWidth};
}

} // namespace

Result<RawRun> readRawRun(std::span<const std::byte> bytes, std::size_t cursor) {
	const auto header = readRunHeader(ByteReader{bytes}, cursor);
	if (!header.hasValue()) {
		return header.error();
	}
	const auto fields = readRunFields(bytes, cursor, header.value());
	if (!fields.hasValue()) {
		return fields.error();
	}
	return makeRawRun(fields.value(), header.value(), cursor);
}

} // namespace revenant::fs::ntfs
