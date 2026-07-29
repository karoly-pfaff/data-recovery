// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/volume/Mbr.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "volume/MbrLayout.hpp"

namespace revenant::volume {

namespace {

// The only two values a status byte may hold. Boot code that happens to sit at
// the table's offset almost never satisfies all four slots at once, which is
// what makes this the real test of whether a sector holds a table.
constexpr std::uint8_t kInactiveStatus = 0x00;
constexpr std::uint8_t kBootableStatus = 0x80;

// Where a slot is and what it says it holds — read before the fields that
// placing it depends on, because an unused slot is not held to their rules.
struct SlotHeader {
	std::uint64_t at;
	std::uint8_t type;
};

// The pair of LBA fields that place a partition, read together because neither
// means anything without the other.
struct LbaExtent {
	std::uint32_t startLba;
	std::uint32_t sectorCount;
};

[[nodiscard]] Result<bool> signatureIsValid(const ByteReader& reader) {
	return reader.readLe<std::uint16_t>(kSignatureOffset).andThen([](std::uint16_t value) {
		if (value != kTableSignature) {
			return Result<bool>(
				Error{.code = ErrorCode::kInvalidArgument, .offset = kSignatureOffset});
		}
		return Result<bool>(true);
	});
}

[[nodiscard]] Result<bool> statusIsValid(const ByteReader& reader, std::uint64_t at) {
	return reader.bytes(at + kStatusField, 1).andThen([at](std::span<const std::byte> raw) {
		const auto value = std::to_integer<std::uint8_t>(raw.front());
		if (value != kInactiveStatus && value != kBootableStatus) {
			return Result<bool>(Error{.code = ErrorCode::kInvalidArgument, .offset = at});
		}
		return Result<bool>(true);
	});
}

[[nodiscard]] Result<SlotHeader> headerAt(const ByteReader& reader, std::uint64_t at) {
	return reader.bytes(at + kTypeField, 1).map([at](std::span<const std::byte> raw) {
		return SlotHeader{.at = at, .type = std::to_integer<std::uint8_t>(raw.front())};
	});
}

[[nodiscard]] Result<LbaExtent> extentAt(const ByteReader& reader, std::uint64_t at) {
	return reader.readLe<std::uint32_t>(at + kStartLbaField).andThen([&](std::uint32_t start) {
		return reader.readLe<std::uint32_t>(at + kSectorCountField)
			.map([start](std::uint32_t count) {
				return LbaExtent{.startLba = start, .sectorCount = count};
			});
	});
}

// An unused slot is taken at its word and its remaining bytes are passed
// through: what sits behind a zeroed type byte is whatever the previous layout
// left there, and holding it to a partition's rules would reject tables that
// every writer produces. A *used* slot must place itself somewhere a partition
// can be — never at LBA 0, which is this table's own sector, and never across
// no sectors at all.
[[nodiscard]] Result<LbaExtent> placedExtent(const LbaExtent& extent, const SlotHeader& slot) {
	if (slot.type == kUnusedPartitionType) {
		return extent;
	}
	if (extent.startLba == 0) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = slot.at + kStartLbaField};
	}
	if (extent.sectorCount == 0) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = slot.at + kSectorCountField};
	}
	return extent;
}

[[nodiscard]] Result<MbrEntry> entryOf(const ByteReader& reader, const SlotHeader& slot) {
	return extentAt(reader, slot.at).andThen([&](const LbaExtent& extent) {
		return placedExtent(extent, slot).map([&](const LbaExtent& placed) {
			return MbrEntry{
				.type = slot.type,
				.startLba = placed.startLba,
				.sectorCount = placed.sectorCount};
		});
	});
}

[[nodiscard]] Result<MbrEntry> entryAt(const ByteReader& reader, std::uint64_t at) {
	return statusIsValid(reader, at)
		.andThen([&](bool) { return headerAt(reader, at); })
		.andThen([&](const SlotHeader& slot) { return entryOf(reader, slot); });
}

[[nodiscard]] Result<MbrTable>
tableWith(const ByteReader& reader, MbrTable table, std::size_t index) {
	return entryAt(reader, slotOffset(index)).map([&table, index](const MbrEntry& entry) {
		table.entries.at(index) = entry;
		return table;
	});
}

// The four slots folded into one table, so that a rejection anywhere is the
// whole table's rejection: a partition list with a slot missing from the middle
// of it would be a worse answer than none.
[[nodiscard]] Result<MbrTable> readTable(const ByteReader& reader) {
	Result<MbrTable> table{MbrTable{}};
	for (std::size_t index = 0; index < kMbrEntryCount; ++index) {
		table = table.andThen(
			[&reader, index](const MbrTable& built) { return tableWith(reader, built, index); });
	}
	return table;
}

} // namespace

Result<MbrTable> parseMbrSector(std::span<const std::byte> sector) {
	if (sector.size() < kMbrSectorBytes) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = sector.size()};
	}
	const ByteReader reader{sector.first(kMbrSectorBytes)};
	return signatureIsValid(reader).andThen([&](bool) { return readTable(reader); });
}

} // namespace revenant::volume
