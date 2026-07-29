// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/ext4/DirectoryEntry.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fs/SlotReader.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::ext4 {

namespace {

constexpr std::size_t kInodeOffset = 0x00;
constexpr std::size_t kRecordLengthOffset = 0x04;
constexpr std::size_t kNameLengthOffset = 0x06;
constexpr std::size_t kFileTypeOffset = 0x07;

// Every record is a multiple of four bytes long: ext4 aligns them so the inode
// number of the next one never straddles a word.
constexpr std::uint16_t kRecordAlignment = 4;

constexpr std::uint8_t kRegularFileCode = 1;
constexpr std::uint8_t kDirectoryCode = 2;

[[nodiscard]] Ext4FileType typeOfCode(std::uint8_t code) {
	if (code == kRegularFileCode) {
		return Ext4FileType::kRegularFile;
	}
	if (code == kDirectoryCode) {
		return Ext4FileType::kDirectory;
	}
	return code == 0 ? Ext4FileType::kUnknown : Ext4FileType::kOther;
}

[[nodiscard]] Result<std::uint16_t>
checkedRecordLength(const ByteReader& reader, std::size_t available) {
	const auto length = slotFieldAt<std::uint16_t>(reader, kRecordLengthOffset);
	const bool usable =
		length >= kDirEntryHeaderBytes && length % kRecordAlignment == 0 && length <= available;
	if (!usable) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = kRecordLengthOffset};
	}
	return length;
}

[[nodiscard]] Result<std::uint8_t>
checkedNameLength(const ByteReader& reader, std::uint16_t record) {
	const auto length = slotByteAt(reader, kNameLengthOffset);
	if (kDirEntryHeaderBytes + length > record) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = kNameLengthOffset};
	}
	return length;
}

[[nodiscard]] std::vector<std::byte> nameOf(std::span<const std::byte> bytes, std::uint8_t length) {
	const auto raw = bytes.subspan(kDirEntryHeaderBytes, length);
	return std::vector<std::byte>{raw.begin(), raw.end()};
}

[[nodiscard]] Result<Ext4DirEntry>
entryOf(const ByteReader& reader, std::span<const std::byte> bytes, std::uint16_t record) {
	return checkedNameLength(reader, record).map([&](std::uint8_t nameLength) {
		return Ext4DirEntry{
			.inode = slotFieldAt<std::uint32_t>(reader, kInodeOffset),
			.recordBytes = record,
			.type = typeOfCode(slotByteAt(reader, kFileTypeOffset)),
			.nameBytes = nameOf(bytes, nameLength)};
	});
}

} // namespace

Result<Ext4DirEntry> parseExt4DirEntry(std::span<const std::byte> bytes) {
	return slotReader(bytes, kDirEntryHeaderBytes).andThen([&bytes](const ByteReader& reader) {
		return checkedRecordLength(reader, bytes.size()).andThen([&](std::uint16_t record) {
			return entryOf(reader, bytes, record);
		});
	});
}

} // namespace revenant::fs::ext4
