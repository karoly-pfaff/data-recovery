// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery/CandidateRecord.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Endian.hpp"

namespace revenant::recovery {

namespace {

// Field positions inside one record. Named rather than spelled inline so the
// writer and the reader below cannot disagree about where a field lives.
constexpr std::size_t kBlobOffsetAt = 0x00;
constexpr std::size_t kCreatedAt = 0x08;
constexpr std::size_t kModifiedAt = 0x10;
constexpr std::size_t kAccessedAt = 0x18;
constexpr std::size_t kNameLengthAt = 0x20;
constexpr std::size_t kResidentLengthAt = 0x24;
constexpr std::size_t kExtentCountAt = 0x28;
constexpr std::size_t kConfidenceAt = 0x2C;
constexpr std::size_t kSourceAt = 0x2D;

constexpr std::size_t kVersionAt = 0x08;
constexpr std::size_t kRecordSizeAt = 0x0C;

// Every on-disk integer is written little-endian explicitly, so an index
// written on one machine reads back on another.
template <std::unsigned_integral T>
void putLe(std::span<std::byte> target, std::size_t offset, T value) {
	std::ranges::copy(
		toLittleEndian<T>(value),
		target.begin() + static_cast<std::ptrdiff_t>(offset));
}

void putTimestamps(std::span<std::byte> raw, const CandidateRecord& record) {
	putLe<std::uint64_t>(raw, kCreatedAt, record.created);
	putLe<std::uint64_t>(raw, kModifiedAt, record.modified);
	putLe<std::uint64_t>(raw, kAccessedAt, record.accessed);
}

// The three lengths that describe the record's blob entry, in the order the
// entry itself lays them out.
void putBlobLengths(std::span<std::byte> raw, const CandidateRecord& record) {
	putLe<std::uint32_t>(raw, kNameLengthAt, record.nameLength);
	putLe<std::uint32_t>(raw, kResidentLengthAt, record.residentLength);
	putLe<std::uint32_t>(raw, kExtentCountAt, record.extentCount);
}

} // namespace

std::array<std::byte, kIndexHeaderBytes> encodeIndexHeader() {
	std::array<std::byte, kIndexHeaderBytes> header{};
	std::ranges::copy(kIndexMagic, header.begin());
	putLe<std::uint32_t>(header, kVersionAt, kIndexVersion);
	putLe<std::uint32_t>(header, kRecordSizeAt, static_cast<std::uint32_t>(kRecordBytes));
	return header;
}

bool headerIsOurs(std::span<const std::byte> raw) {
	if (raw.size() < kIndexHeaderBytes ||
		!std::ranges::equal(raw.first(kIndexMagic.size()), kIndexMagic)) {
		return false;
	}
	const ByteReader reader{raw};
	return reader.readLe<std::uint32_t>(kVersionAt).value() == kIndexVersion &&
		   reader.readLe<std::uint32_t>(kRecordSizeAt).value() == kRecordBytes;
}

std::array<std::byte, kRecordBytes> encodeRecord(const CandidateRecord& record) {
	std::array<std::byte, kRecordBytes> raw{};
	putLe<std::uint64_t>(raw, kBlobOffsetAt, record.blobOffset);
	putTimestamps(raw, record);
	putBlobLengths(raw, record);
	putLe<std::uint8_t>(raw, kConfidenceAt, record.confidence);
	putLe<std::uint8_t>(raw, kSourceAt, record.source);
	return raw;
}

CandidateRecord decodeRecord(std::span<const std::byte> raw) {
	// The caller only ever passes a whole record's worth of bytes, so every
	// field below is provably in range and none of these reads can fail.
	const ByteReader reader{raw.first(kRecordBytes)};
	return CandidateRecord{
		.blobOffset = reader.readLe<std::uint64_t>(kBlobOffsetAt).value(),
		.created = reader.readLe<std::uint64_t>(kCreatedAt).value(),
		.modified = reader.readLe<std::uint64_t>(kModifiedAt).value(),
		.accessed = reader.readLe<std::uint64_t>(kAccessedAt).value(),
		.nameLength = reader.readLe<std::uint32_t>(kNameLengthAt).value(),
		.residentLength = reader.readLe<std::uint32_t>(kResidentLengthAt).value(),
		.extentCount = reader.readLe<std::uint32_t>(kExtentCountAt).value(),
		.confidence = reader.readLe<std::uint8_t>(kConfidenceAt).value(),
		.source = reader.readLe<std::uint8_t>(kSourceAt).value()};
}

} // namespace revenant::recovery
