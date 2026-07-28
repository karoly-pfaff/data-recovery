// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/ntfs/MftTable.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "imagegen/ntfs/AttributeBuilder.hpp"
#include "imagegen/ntfs/FixtureFiles.hpp"
#include "imagegen/ntfs/MftRecordBuilder.hpp"
#include "imagegen/ntfs/NtfsLayout.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/fs/ntfs/MftRecord.hpp"
#include "revenant/fs/ntfs/Runlist.hpp"
#include "support/InMemoryDevice.hpp"
#include "support/NtfsVolume.hpp"

namespace {

using revenant::ErrorCode;
using revenant::fs::ntfs::DataRun;
using revenant::fs::ntfs::kMftRecordNumber;
using revenant::fs::ntfs::MftTable;
using revenant::imagegen::ntfs::buildEndMarker;
using revenant::imagegen::ntfs::buildFileName;
using revenant::imagegen::ntfs::buildMftRecord;
using revenant::imagegen::ntfs::buildNonResidentData;
using revenant::imagegen::ntfs::buildResidentData;
using revenant::imagegen::ntfs::buildStandardInformation;
using revenant::imagegen::ntfs::FileNameSpec;
using revenant::imagegen::ntfs::kDeletedJpegRecord;
using revenant::imagegen::ntfs::kRootRecord;
using revenant::imagegen::ntfs::makeLayout;
using revenant::imagegen::ntfs::MftRecordSpec;
using revenant::imagegen::ntfs::NonResidentDataSpec;
using revenant::testing::InMemoryDevice;
using revenant::testing::NtfsVolume;
using revenant::testing::recordOffset;
using revenant::testing::VolumeRange;

// The MFT is 8 clusters; splitting it in half puts records 16-31 somewhere else
// entirely, which is the point of reading through an extent list.
constexpr std::uint64_t kMftHalfClusters = 4;
constexpr std::uint64_t kRelocatedHalfCluster = 50;

[[nodiscard]] std::uint64_t mftSizeBytes() {
	const auto layout = makeLayout();
	return std::uint64_t{layout.mftRecordCount} * layout.mftRecordBytes;
}

// Record 0 as the fixture writes it, except for the `$DATA` the caller supplies
// — the one attribute every test here varies.
[[nodiscard]] std::vector<std::byte> mftRecordWithData(const std::vector<std::byte>& data) {
	std::vector<std::byte> attributes;
	for (const auto& attribute :
		 {buildStandardInformation(),
		  buildFileName(
			  FileNameSpec{
				  .parentRecord = kRootRecord,
				  .parentSequence = 1,
				  .name = "$MFT",
				  .realSize = mftSizeBytes()}),
		  data,
		  buildEndMarker()}) {
		attributes.insert(attributes.end(), attribute.begin(), attribute.end());
	}
	return buildMftRecord(
		makeLayout(),
		MftRecordSpec{
			.sequence = 1,
			.inUse = true,
			.isDirectory = false,
			.attributes = attributes});
}

[[nodiscard]] std::vector<std::byte> nonResidentMftData(const std::vector<DataRun>& runs) {
	return buildNonResidentData(
		NonResidentDataSpec{
			.runs = runs,
			.realSize = mftSizeBytes(),
			.bytesPerCluster = makeLayout().bytesPerCluster()});
}

[[nodiscard]] std::string nameOf(const MftTable& table, std::uint64_t number) {
	const auto parsed = table.readRecord(number);
	return parsed.hasValue() && !parsed.value().names.empty()
			   ? parsed.value().names.front().name.utf8
			   : std::string{"<unreadable>"};
}

TEST(NtfsMftTable, ReportsTheRecordCountItsOwnDataDeclares) {
	NtfsVolume volume;
	const auto table = volume.openTable();
	ASSERT_TRUE(table.hasValue());
	EXPECT_EQ(table.value().recordCount(), makeLayout().mftRecordCount);
}

TEST(NtfsMftTable, CarriesTheGeometryItWasOpenedWith) {
	NtfsVolume volume;
	const auto table = volume.openTable();
	ASSERT_TRUE(table.hasValue());
	EXPECT_EQ(table.value().geometry().bytesPerMftRecord, makeLayout().mftRecordBytes);
}

TEST(NtfsMftTable, ReadsRecordZeroBackAsTheMftItself) {
	NtfsVolume volume;
	const auto table = volume.openTable();
	ASSERT_TRUE(table.hasValue());
	EXPECT_EQ(nameOf(table.value(), kMftRecordNumber), "$MFT");
}

TEST(NtfsMftTable, ReadsAUserRecordByItsNumber) {
	NtfsVolume volume;
	const auto table = volume.openTable();
	ASSERT_TRUE(table.hasValue());
	EXPECT_EQ(nameOf(table.value(), kDeletedJpegRecord), "deleted.jpg");
}

TEST(NtfsMftTable, SkipsAnEmptySlotAsNotFoundRatherThanDamage) {
	NtfsVolume volume;
	const auto table = volume.openTable();
	ASSERT_TRUE(table.hasValue());
	const auto parsed = table.value().readRecord(1);
	ASSERT_FALSE(parsed.hasValue());
	EXPECT_EQ(parsed.error().code, ErrorCode::kNotFound);
}

TEST(NtfsMftTable, RejectsARecordNumberPastTheTable) {
	NtfsVolume volume;
	const auto table = volume.openTable();
	ASSERT_TRUE(table.hasValue());
	const auto parsed = table.value().readRecord(makeLayout().mftRecordCount);
	ASSERT_FALSE(parsed.hasValue());
	EXPECT_EQ(parsed.error().code, ErrorCode::kOutOfRange);
}

TEST(NtfsMftTable, RefusesAVolumeWhoseRecordZeroIsNotAFileRecord) {
	NtfsVolume volume;
	volume.putRecord(kMftRecordNumber, std::vector<std::byte>(makeLayout().mftRecordBytes));
	const auto table = volume.openTable();
	ASSERT_FALSE(table.hasValue());
	EXPECT_EQ(table.error().code, ErrorCode::kNotFound);
}

// The MFT cannot fit inside one of its own records, so a record 0 claiming that
// is damaged metadata, not a table with an empty extent list.
TEST(NtfsMftTable, RefusesAnMftWhoseOwnDataIsResident) {
	NtfsVolume volume;
	volume.putRecord(
		kMftRecordNumber,
		mftRecordWithData(buildResidentData(std::vector<std::byte>(16, std::byte{0x41}))));
	const auto table = volume.openTable();
	ASSERT_FALSE(table.hasValue());
	EXPECT_EQ(table.error().code, ErrorCode::kInvalidArgument);
}

TEST(NtfsMftTable, RefusesAnMftWithNoDataAttributeAtAll) {
	NtfsVolume volume;
	volume.putRecord(kMftRecordNumber, mftRecordWithData({}));
	const auto table = volume.openTable();
	ASSERT_FALSE(table.hasValue());
	EXPECT_EQ(table.error().code, ErrorCode::kInvalidArgument);
}

// A device that ends inside the table the metadata describes — a truncated
// image, or a volume cut short. Half a record is not a record.
TEST(NtfsMftTable, RejectsARecordTheDeviceIsTooShortToHold) {
	NtfsVolume volume;
	const auto image =
		volume.bytes().first(static_cast<std::size_t>(recordOffset(kMftRecordNumber + 1)));
	InMemoryDevice device{
		std::vector<std::byte>{image.begin(), image.end()},
		makeLayout().bytesPerSector};
	const auto table = MftTable::open(device, volume.geometry());
	ASSERT_TRUE(table.hasValue());
	const auto parsed = table.value().readRecord(kDeletedJpegRecord);
	ASSERT_FALSE(parsed.hasValue());
	EXPECT_EQ(parsed.error().code, ErrorCode::kOutOfRange);
}

// A fragmented `$MFT`: the second half of the table is physically elsewhere, so
// a record past the split is only readable through the extent list.
TEST(NtfsMftTable, ReadsARecordFromTheSecondFragmentOfASplitMft) {
	const auto layout = makeLayout();
	const auto halfBytes = kMftHalfClusters * layout.bytesPerCluster();
	const VolumeRange secondHalf{
		.offset = layout.mftOffsetBytes() + halfBytes,
		.length = static_cast<std::size_t>(halfBytes)};
	NtfsVolume volume;
	volume.copyWithin(secondHalf, layout.clusterOffsetBytes(kRelocatedHalfCluster));
	volume.clear(secondHalf);
	volume.putRecord(
		kMftRecordNumber,
		mftRecordWithData(nonResidentMftData(
			{DataRun{
				 .startCluster = layout.mftStartCluster,
				 .lengthClusters = kMftHalfClusters,
				 .sparse = false},
			 DataRun{
				 .startCluster = kRelocatedHalfCluster,
				 .lengthClusters = kMftHalfClusters,
				 .sparse = false}})));
	const auto table = volume.openTable();
	ASSERT_TRUE(table.hasValue());
	EXPECT_EQ(nameOf(table.value(), kDeletedJpegRecord), "deleted.jpg");
}

} // namespace
