// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/exfat/DirectoryEntry.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fs/DosTime.hpp"
#include "fs/SlotReader.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::fs::exfat {

namespace {

// Bit 7 of the type byte says the set is still in use; the low seven identify
// what kind of entry it is. Clearing that one bit — and nothing else — is all
// exFAT does to delete a file.
constexpr std::uint8_t kInUseBit = 0x80;
constexpr std::uint8_t kTypeCodeMask = 0x7F;

constexpr std::uint8_t kFileCode = 0x05;
constexpr std::uint8_t kStreamExtensionCode = 0x40;
constexpr std::uint8_t kFileNameCode = 0x41;
constexpr std::uint8_t kAllocationBitmapCode = 0x01;
constexpr std::uint8_t kUpcaseTableCode = 0x02;
constexpr std::uint8_t kVolumeLabelCode = 0x03;

// The directory bit of a file entry's attribute field.
constexpr std::uint16_t kDirectoryAttribute = 0x0010;
// Bit 1 of the secondary flags: the clusters follow one another, so the FAT
// holds nothing for this file.
constexpr std::uint8_t kNoFatChainFlag = 0x02;

constexpr std::size_t kNameFragmentOffset = 0x02;
constexpr std::size_t kNameFragmentBytes = 30;

// The types that are the volume's own bookkeeping rather than part of a file.
[[nodiscard]] ExfatEntryKind kindOfMetadataCode(std::uint8_t code) {
	if (code == kAllocationBitmapCode) {
		return ExfatEntryKind::kAllocationBitmap;
	}
	if (code == kUpcaseTableCode) {
		return ExfatEntryKind::kUpcaseTable;
	}
	return code == kVolumeLabelCode ? ExfatEntryKind::kVolumeLabel : ExfatEntryKind::kUnknown;
}

[[nodiscard]] ExfatEntryKind kindOfCode(std::uint8_t code) {
	if (code == kFileCode) {
		return ExfatEntryKind::kFile;
	}
	if (code == kStreamExtensionCode) {
		return ExfatEntryKind::kStreamExtension;
	}
	if (code == kFileNameCode) {
		return ExfatEntryKind::kFileName;
	}
	return kindOfMetadataCode(code);
}

// exFAT packs a DOS date and time into one 32-bit field, the time in the low
// half. The conversion itself is the layer's, shared with FAT.
[[nodiscard]] std::uint64_t stampAt(const ByteReader& reader, std::size_t offset) {
	const auto packed = slotFieldAt<std::uint32_t>(reader, offset);
	return toFiletime(
		DosTimestamp{
			.date = static_cast<std::uint16_t>(packed >> 16U),
			.time = static_cast<std::uint16_t>(packed & 0xFFFFU)});
}

} // namespace

Result<ExfatEntryHeader> classifyExfatEntry(std::span<const std::byte> slot) {
	return slotReader(slot, kDirectoryEntryBytes).map([](const ByteReader& reader) {
		const auto type = slotByteAt(reader, 0);
		if (type == 0) {
			return ExfatEntryHeader{.kind = ExfatEntryKind::kEndOfDirectory, .inUse = false};
		}
		return ExfatEntryHeader{
			.kind = kindOfCode(type & kTypeCodeMask),
			.inUse = (type & kInUseBit) != 0};
	});
}

Result<FileEntry> parseFileEntry(std::span<const std::byte> slot) {
	return slotReader(slot, kDirectoryEntryBytes).map([](const ByteReader& reader) {
		return FileEntry{
			.secondaryCount = slotByteAt(reader, 0x01),
			.timestamps =
				Timestamps{
					.created = stampAt(reader, 0x08),
					.modified = stampAt(reader, 0x0C),
					.accessed = stampAt(reader, 0x10)},
			.isDirectory = (slotFieldAt<std::uint16_t>(reader, 0x04) & kDirectoryAttribute) != 0};
	});
}

Result<StreamExtension> parseStreamExtension(std::span<const std::byte> slot) {
	return slotReader(slot, kDirectoryEntryBytes).map([](const ByteReader& reader) {
		return StreamExtension{
			.firstCluster = slotFieldAt<std::uint32_t>(reader, 0x14),
			.validDataLength = slotFieldAt<std::uint64_t>(reader, 0x08),
			.dataLength = slotFieldAt<std::uint64_t>(reader, 0x18),
			.nameLength = slotByteAt(reader, 0x03),
			.noFatChain = (slotByteAt(reader, 0x01) & kNoFatChainFlag) != 0};
	});
}

Result<std::vector<std::byte>> parseFileName(std::span<const std::byte> slot) {
	return slotReader(slot, kDirectoryEntryBytes).map([&slot](const ByteReader&) {
		const auto fragment = slot.subspan(kNameFragmentOffset, kNameFragmentBytes);
		return std::vector<std::byte>{fragment.begin(), fragment.end()};
	});
}

} // namespace revenant::fs::exfat
