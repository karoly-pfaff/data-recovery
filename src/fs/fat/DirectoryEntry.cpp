// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/fat/DirectoryEntry.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include "fs/DosTime.hpp"
#include "fs/SlotReader.hpp"
#include "fs/fat/DirectoryEntryInternal.hpp"
#include "fs/fat/ShortName.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/Utf16Name.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::fs::fat {

namespace {

[[nodiscard]] EntryKind kindOfAttributes(std::uint8_t attributes) {
	if ((attributes & kAttrKindMask) == kAttrLongName) {
		return EntryKind::kLongName;
	}
	if ((attributes & kAttrVolumeLabel) != 0U) {
		return EntryKind::kVolumeLabel;
	}
	return (attributes & kAttrDirectory) != 0U ? EntryKind::kDirectory : EntryKind::kFile;
}

[[nodiscard]] std::uint32_t clusterOf(const ByteReader& reader) {
	const auto high =
		static_cast<std::uint32_t>(slotFieldAt<std::uint16_t>(reader, kClusterHighOffset));
	const auto low =
		static_cast<std::uint32_t>(slotFieldAt<std::uint16_t>(reader, kClusterLowOffset));
	return (high << 16U) | low;
}

[[nodiscard]] std::uint64_t stampAt(const ByteReader& reader, std::size_t time, std::size_t date) {
	return toFiletime(
		DosTimestamp{
			.date = slotFieldAt<std::uint16_t>(reader, date),
			.time = slotFieldAt<std::uint16_t>(reader, time)});
}

// FAT records a date but no time for the last access, so the stamp lands at
// midnight. That is the format's precision, not a field this parser dropped.
[[nodiscard]] std::uint64_t accessedAt(const ByteReader& reader) {
	return toFiletime(
		DosTimestamp{.date = slotFieldAt<std::uint16_t>(reader, kAccessedDateOffset), .time = 0});
}

[[nodiscard]] Timestamps timestampsOf(const ByteReader& reader) {
	return Timestamps{
		.created = stampAt(reader, kCreatedTimeOffset, kCreatedDateOffset),
		.modified = stampAt(reader, kWrittenTimeOffset, kWrittenDateOffset),
		.accessed = accessedAt(reader)};
}

[[nodiscard]] DecodedName nameOf(const ByteReader& reader, bool deleted) {
	const auto raw = reader.bytes(kNameOffset, kNameBytes);
	if (!raw.hasValue()) {
		return DecodedName{.utf8 = {}, .lossless = false};
	}
	return decodeShortName(raw.value(), slotByteAt(reader, kCaseFlagsOffset), deleted);
}

[[nodiscard]] ShortEntry entryFrom(const ByteReader& reader) {
	const bool deleted = slotByteAt(reader, kNameOffset) == kDeletedMarker;
	return ShortEntry{
		.name = nameOf(reader, deleted),
		.firstCluster = clusterOf(reader),
		.sizeInBytes = slotFieldAt<std::uint32_t>(reader, kSizeOffset),
		.timestamps = timestampsOf(reader),
		.deleted = deleted};
}

} // namespace

Result<EntryKind> classifyEntry(std::span<const std::byte> slot) {
	return slotReader(slot, kDirectoryEntryBytes).map([](const ByteReader& reader) {
		if (slotByteAt(reader, kNameOffset) == kEndOfDirectoryMarker) {
			return EntryKind::kEndOfDirectory;
		}
		return kindOfAttributes(slotByteAt(reader, kAttributesOffset));
	});
}

Result<ShortEntry> parseShortEntry(std::span<const std::byte> slot) {
	return slotReader(slot, kDirectoryEntryBytes).map(entryFrom);
}

} // namespace revenant::fs::fat
