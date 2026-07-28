// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/ntfs/MftTable.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "fs/ExtentLocate.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/ntfs/BootSector.hpp"
#include "revenant/fs/ntfs/MftRecord.hpp"
#include "revenant/fs/ntfs/Runlist.hpp"

namespace revenant::fs::ntfs {

namespace {

// Exactly the bytes `extent` names. A short read is kOutOfRange rather than a
// partial buffer: a half-read MFT record is not a record.
[[nodiscard]] Result<std::vector<std::byte>> readBlock(BlockDevice& device, const Extent& extent) {
	std::vector<std::byte> raw(static_cast<std::size_t>(extent.lengthBytes), std::byte{0});
	const auto read = device.readAt(extent.deviceOffset, raw);
	if (!read.hasValue()) {
		return read.error();
	}
	if (read.value() != raw.size()) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = extent.deviceOffset};
	}
	return raw;
}

[[nodiscard]] Result<MftRecordView>
parseBlockAsRecord(const Result<std::vector<std::byte>>& block, std::uint64_t number) {
	return block.andThen(
		[number](const std::vector<std::byte>& raw) { return parseMftRecord(raw, number); });
}

// Record 0 sits at the boot sector's declared MFT offset — the one record whose
// position is known without already having the table.
[[nodiscard]] Result<MftRecordView>
readRecordZero(BlockDevice& device, const NtfsGeometry& geometry) {
	const Extent recordZero{
		.deviceOffset = geometry.mftOffsetBytes,
		.lengthBytes = geometry.bytesPerMftRecord};
	return parseBlockAsRecord(readBlock(device, recordZero), kMftRecordNumber);
}

// The MFT's own `$DATA` must exist and be non-resident: a table that fits
// inside one of its own records is damaged metadata, not an empty table.
[[nodiscard]] bool hasNonResidentData(const MftRecordView& view) {
	return view.data.has_value() && !view.data->resident;
}

} // namespace

MftTable::MftTable(BlockDevice& device, const NtfsGeometry& geometry, Layout layout)
	: device_(&device), geometry_(geometry), layout_(std::move(layout)) {}

Result<MftTable::Layout>
MftTable::layoutFrom(const MftRecordView& recordZero, const NtfsGeometry& geometry) {
	if (!hasNonResidentData(recordZero)) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = geometry.mftOffsetBytes};
	}
	const MftData& data = *recordZero.data;
	return decodeRunlist(data.runlistBytes)
		.andThen([&](const Runlist& runlist) {
			return runlistExtents(runlist, geometry, data.realSize);
		})
		.map([&](const std::vector<Extent>& extents) {
			return Layout{
				.extents = extents,
				.recordCount = data.realSize / geometry.bytesPerMftRecord};
		});
}

Result<MftTable> MftTable::open(BlockDevice& device, const NtfsGeometry& geometry) {
	const auto recordZero = readRecordZero(device, geometry);
	if (!recordZero.hasValue()) {
		return recordZero.error();
	}
	const auto layout = layoutFrom(recordZero.value(), geometry);
	if (!layout.hasValue()) {
		return layout.error();
	}
	return MftTable{device, geometry, layout.value()};
}

std::uint64_t MftTable::recordCount() const noexcept {
	return layout_.recordCount;
}

const NtfsGeometry& MftTable::geometry() const noexcept {
	return geometry_;
}

Result<std::uint64_t> MftTable::recordOffset(std::uint64_t number) const {
	if (number >= layout_.recordCount) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = number};
	}
	const auto recordBytes = static_cast<std::uint64_t>(geometry_.bytesPerMftRecord);
	return locateInExtents(
		layout_.extents,
		FileRange{.offset = number * recordBytes, .length = recordBytes});
}

Result<MftRecordView> MftTable::readRecord(std::uint64_t number) const {
	const auto offset = recordOffset(number);
	if (!offset.hasValue()) {
		return offset.error();
	}
	const Extent record{.deviceOffset = offset.value(), .lengthBytes = geometry_.bytesPerMftRecord};
	return parseBlockAsRecord(readBlock(*device_, record), number);
}

} // namespace revenant::fs::ntfs
