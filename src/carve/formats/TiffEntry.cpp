// SPDX-License-Identifier: GPL-3.0-or-later
#include "TiffEntry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

namespace {

constexpr std::uint64_t kInlineValueBytes = 4;
constexpr std::size_t kEntryBytes = 12;
constexpr std::uint16_t kShortType = 3;
constexpr std::uint16_t kLongType = 4;
constexpr std::uint32_t kShortBytes = 2;
constexpr std::uint32_t kLongBytes = 4;

// Byte counts for the TIFF field types, indexed by type code. Zero marks a
// code this decoder does not know; entry 0 is unused by the format.
constexpr std::array<std::uint64_t, 13> kTypeBytes{0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8};

// The value of a SHORT/LONG element at a byte offset, in the file's order.
// `type` leads so the context separates it from `offset`: two adjacent,
// mutually convertible integers would be a swap waiting to happen.
[[nodiscard]] Result<std::uint32_t>
readElementAt(std::uint16_t type, const TiffContext& tiff, std::uint64_t offset) {
	if (type == kShortType) {
		return readU16(tiff, offset).map([](std::uint16_t value) {
			return static_cast<std::uint32_t>(value);
		});
	}
	if (type == kLongType) {
		return readU32(tiff, offset);
	}
	return Error{.code = ErrorCode::kInvalidArgument, .offset = offset};
}

// A single inline element is the entry's own value field. A packed pair cannot
// be read without the entry's own address, so it is refused rather than
// guessed at — an unusable entry bounds the extent, which is the honest result.
[[nodiscard]] Result<std::uint32_t> inlineElement(const TiffEntry& entry) {
	if (entry.count != 1) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = entry.tag};
	}
	return entry.valueOffset;
}

// Where the `index`-th element sits inside the block the entry points at.
[[nodiscard]] std::uint64_t elementOffset(const TiffEntry& entry, std::uint32_t index) {
	const auto stride = entry.type == kShortType ? kShortBytes : kLongBytes;
	return entry.valueOffset + (static_cast<std::uint64_t>(index) * stride);
}

} // namespace

Result<std::uint16_t> readU16(const TiffContext& tiff, std::uint64_t offset) {
	if (tiff.bigEndian) {
		return tiff.reader.readBe<std::uint16_t>(offset);
	}
	return tiff.reader.readLe<std::uint16_t>(offset);
}

Result<std::uint32_t> readU32(const TiffContext& tiff, std::uint64_t offset) {
	if (tiff.bigEndian) {
		return tiff.reader.readBe<std::uint32_t>(offset);
	}
	return tiff.reader.readLe<std::uint32_t>(offset);
}

Result<TiffEntry> readEntry(const TiffContext& tiff, std::uint64_t offset) {
	if (!tiff.reader.bytes(offset, kEntryBytes).hasValue()) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = offset};
	}
	return TiffEntry{
		.tag = readU16(tiff, offset).value(),
		.type = readU16(tiff, offset + 2).value(),
		.count = readU32(tiff, offset + 4).value(),
		.valueOffset = readU32(tiff, offset + 8).value()};
}

std::uint64_t valueBytes(const TiffEntry& entry) {
	if (entry.type >= kTypeBytes.size()) {
		return 0;
	}
	return kTypeBytes.at(entry.type) * static_cast<std::uint64_t>(entry.count);
}

std::uint64_t valueEnd(const TiffEntry& entry) {
	const auto size = valueBytes(entry);
	if (size <= kInlineValueBytes) {
		return 0;
	}
	return static_cast<std::uint64_t>(entry.valueOffset) + size;
}

Result<std::uint32_t>
arrayElement(const TiffContext& tiff, const TiffEntry& entry, std::uint32_t index) {
	if (index >= entry.count || (entry.type != kShortType && entry.type != kLongType)) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = entry.tag};
	}
	if (valueBytes(entry) <= kInlineValueBytes) {
		return inlineElement(entry);
	}
	return readElementAt(entry.type, tiff, elementOffset(entry, index));
}

} // namespace revenant::carve
