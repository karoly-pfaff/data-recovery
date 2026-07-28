// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/fat/DirectoryEntry.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include "fs/fat/DirectoryEntryInternal.hpp"
#include "fs/fat/DosTime.hpp"
#include "fs/fat/ShortName.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/NameDecode.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::fs::fat {

namespace {

[[nodiscard]] Result<ByteReader> slotReader(std::span<const std::byte> slot) {
	if (slot.size() < kDirectoryEntryBytes) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = slot.size()};
	}
	return ByteReader{slot.first(kDirectoryEntryBytes)};
}

// A read at a fixed offset inside a slot already sized to a whole entry cannot
// fail, so these unwrap to a value rather than threading a Result nobody can
// act on. The size check in `slotReader` is what makes that true.
[[nodiscard]] std::uint8_t byteAt(const ByteReader& reader, std::size_t offset) {
	const auto raw = reader.bytes(offset, 1);
	return raw.hasValue() ? std::to_integer<std::uint8_t>(raw.value().front()) : 0U;
}

template <typename T> [[nodiscard]] T fieldAt(const ByteReader& reader, std::size_t offset) {
	const auto raw = reader.readLe<T>(offset);
	return raw.hasValue() ? raw.value() : T{0};
}

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
		static_cast<std::uint32_t>(fieldAt<std::uint16_t>(reader, kClusterHighOffset));
	const auto low = static_cast<std::uint32_t>(fieldAt<std::uint16_t>(reader, kClusterLowOffset));
	return (high << 16U) | low;
}

[[nodiscard]] std::uint64_t stampAt(const ByteReader& reader, std::size_t time, std::size_t date) {
	return toFiletime(
		DosTimestamp{
			.date = fieldAt<std::uint16_t>(reader, date),
			.time = fieldAt<std::uint16_t>(reader, time)});
}

// FAT records a date but no time for the last access, so the stamp lands at
// midnight. That is the format's precision, not a field this parser dropped.
[[nodiscard]] std::uint64_t accessedAt(const ByteReader& reader) {
	return toFiletime(
		DosTimestamp{.date = fieldAt<std::uint16_t>(reader, kAccessedDateOffset), .time = 0});
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
	return decodeShortName(raw.value(), byteAt(reader, kCaseFlagsOffset), deleted);
}

[[nodiscard]] ShortEntry entryFrom(const ByteReader& reader) {
	const bool deleted = byteAt(reader, kNameOffset) == kDeletedMarker;
	return ShortEntry{
		.name = nameOf(reader, deleted),
		.firstCluster = clusterOf(reader),
		.sizeInBytes = fieldAt<std::uint32_t>(reader, kSizeOffset),
		.timestamps = timestampsOf(reader),
		.deleted = deleted};
}

} // namespace

Result<EntryKind> classifyEntry(std::span<const std::byte> slot) {
	return slotReader(slot).map([](const ByteReader& reader) {
		if (byteAt(reader, kNameOffset) == kEndOfDirectoryMarker) {
			return EntryKind::kEndOfDirectory;
		}
		return kindOfAttributes(byteAt(reader, kAttributesOffset));
	});
}

Result<ShortEntry> parseShortEntry(std::span<const std::byte> slot) {
	return slotReader(slot).map(entryFrom);
}

} // namespace revenant::fs::fat
