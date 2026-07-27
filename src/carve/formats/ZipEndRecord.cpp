// SPDX-License-Identifier: GPL-3.0-or-later
#include "ZipEndRecord.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

namespace {

constexpr std::size_t kEndRecordBytes = 22;
constexpr std::uint64_t kEntryCountOffset = 10;
constexpr std::uint64_t kDirectorySizeOffset = 12;
constexpr std::uint64_t kDirectoryOffsetOffset = 16;
constexpr std::uint64_t kCommentLengthOffset = 20;

constexpr std::array<std::byte, 4> kEndSignature{
	std::byte{0x50},
	std::byte{0x4B},
	std::byte{0x05},
	std::byte{0x06}};
constexpr std::array<std::byte, 4> kDirectorySignature{
	std::byte{0x50},
	std::byte{0x4B},
	std::byte{0x01},
	std::byte{0x02}};

// The offset of the last EOCD signature in the data. The *last* one matters:
// an archive may legitimately contain another archive's bytes, and stopping at
// the first would cut the outer file short.
[[nodiscard]] Result<std::uint64_t> lastSignatureOffset(const ByteReader& reader) {
	const auto all = reader.bytes(0, reader.size());
	if (!all.hasValue()) {
		return Error{.code = ErrorCode::kNotFound, .offset = 0};
	}
	const auto found = std::ranges::find_end(all.value(), kEndSignature);
	if (found.empty()) {
		return Error{.code = ErrorCode::kNotFound, .offset = 0};
	}
	return static_cast<std::uint64_t>(std::distance(all.value().begin(), found.begin()));
}

// A real EOCD's directory arithmetic lands exactly on the record itself, and
// the offset it names begins a central directory header. Both must hold; a
// signature that satisfies neither is a coincidence in the data.
[[nodiscard]] bool
directoryChecksOut(const ByteReader& reader, std::uint64_t eocd, std::uint64_t directoryOffset) {
	const auto size = reader.readLe<std::uint32_t>(eocd + kDirectorySizeOffset);
	if (!size.hasValue() || directoryOffset + size.value() != eocd) {
		return false;
	}
	const auto head = reader.bytes(directoryOffset, kDirectorySignature.size());
	return head.hasValue() && std::ranges::equal(head.value(), kDirectorySignature);
}

[[nodiscard]] Result<ZipEndRecord> readEndRecord(const ByteReader& reader, std::uint64_t offset) {
	const auto comment = reader.readLe<std::uint16_t>(offset + kCommentLengthOffset);
	if (!comment.hasValue()) {
		return comment.error();
	}
	const auto directory = reader.readLe<std::uint32_t>(offset + kDirectoryOffsetOffset);
	if (!directory.hasValue()) {
		return directory.error();
	}
	return ZipEndRecord{
		.offset = offset,
		.end = offset + kEndRecordBytes + comment.value(),
		.centralDirectoryOffset = directory.value(),
		.entryCount = reader.readLe<std::uint16_t>(offset + kEntryCountOffset).value(),
		.centralDirectoryChecksOut = directoryChecksOut(reader, offset, directory.value())};
}

} // namespace

Result<ZipEndRecord> findZipEndRecord(const ByteReader& reader) {
	return lastSignatureOffset(reader).andThen(
		[&reader](std::uint64_t offset) { return readEndRecord(reader, offset); });
}

} // namespace revenant::carve
