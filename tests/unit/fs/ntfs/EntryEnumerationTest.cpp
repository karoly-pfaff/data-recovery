// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/fs/ntfs/EntryEnumeration.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "imagegen/ntfs/AttributeBuilder.hpp"
#include "imagegen/ntfs/FixtureFiles.hpp"
#include "imagegen/ntfs/MftRecordBuilder.hpp"
#include "imagegen/ntfs/NtfsLayout.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/ntfs/MftTable.hpp"
#include "revenant/fs/ntfs/Runlist.hpp"
#include "support/CollectingEntryVisitor.hpp"
#include "support/InMemoryDevice.hpp"
#include "support/NtfsVolume.hpp"

namespace {

using revenant::BlockDevice;
using revenant::Confidence;
using revenant::Error;
using revenant::ErrorCode;
using revenant::Result;
using revenant::fs::EntryState;
using revenant::fs::Extent;
using revenant::fs::RecoveredEntry;
using revenant::fs::ntfs::DataRun;
using revenant::fs::ntfs::enumerateEntries;
using revenant::fs::ntfs::kFirstUserRecord;
using revenant::fs::ntfs::MftTable;
using revenant::imagegen::ntfs::buildEndMarker;
using revenant::imagegen::ntfs::buildFileName;
using revenant::imagegen::ntfs::buildMftRecord;
using revenant::imagegen::ntfs::buildNonResidentData;
using revenant::imagegen::ntfs::buildStandardInformation;
using revenant::imagegen::ntfs::FileNameSpec;
using revenant::imagegen::ntfs::fixtureFiles;
using revenant::imagegen::ntfs::kDeletedJpegRecord;
using revenant::imagegen::ntfs::kDeletedNotesRecord;
using revenant::imagegen::ntfs::kFixtureCreated;
using revenant::imagegen::ntfs::kKeepJpegRecord;
using revenant::imagegen::ntfs::kRootRecord;
using revenant::imagegen::ntfs::makeLayout;
using revenant::imagegen::ntfs::MftRecordSpec;
using revenant::imagegen::ntfs::NonResidentDataSpec;
using revenant::testing::CollectingEntryVisitor;
using revenant::testing::InMemoryDevice;
using revenant::testing::NtfsVolume;

// A device that reads normally up to `faultOffset` and reports a hardware fault
// past it — a bad sector partway through the MFT.
class FaultingDevice final : public BlockDevice {
public:
	FaultingDevice(std::vector<std::byte> data, std::uint64_t faultOffset)
		: inner_(std::move(data), makeLayout().bytesPerSector), faultOffset_(faultOffset) {}

	[[nodiscard]] std::uint64_t sizeInBytes() const override {
		return inner_.sizeInBytes();
	}

	[[nodiscard]] std::uint32_t sectorSize() const override {
		return inner_.sectorSize();
	}

	[[nodiscard]] Result<std::size_t>
	readAt(std::uint64_t offset, std::span<std::byte> buffer) override {
		if (offset >= faultOffset_) {
			return Error{.code = ErrorCode::kIoFailure, .offset = offset};
		}
		return inner_.readAt(offset, buffer);
	}

private:
	InMemoryDevice inner_;
	std::uint64_t faultOffset_;
};

// What an entry amounts to, in one comparable value: asserting on the whole
// thing gives one readable diff instead of three separate failures.
struct EntrySummary {
	std::string path;
	EntryState state{};
	Confidence recoverability{};

	friend bool operator==(const EntrySummary&, const EntrySummary&) = default;
};

std::ostream& operator<<(std::ostream& out, const EntrySummary& summary) {
	return out << summary.path << " state " << static_cast<int>(summary.state) << " grade "
			   << static_cast<int>(summary.recoverability);
}

[[nodiscard]] EntrySummary summarize(const RecoveredEntry& entry) {
	return EntrySummary{
		.path = entry.path,
		.state = entry.state,
		.recoverability = entry.recoverability};
}

[[nodiscard]] std::vector<EntrySummary> summarize(const std::vector<RecoveredEntry>& entries) {
	std::vector<EntrySummary> summaries;
	summaries.reserve(entries.size());
	for (const auto& entry : entries) {
		summaries.push_back(summarize(entry));
	}
	return summaries;
}

[[nodiscard]] const RecoveredEntry*
entryNamed(const std::vector<RecoveredEntry>& entries, std::string_view path) {
	const auto found = std::ranges::find(entries, path, &RecoveredEntry::path);
	return found != entries.end() ? &*found : nullptr;
}

[[nodiscard]] std::vector<std::byte> fixtureContent(std::uint64_t recordNumber) {
	for (auto& file : fixtureFiles(makeLayout())) {
		if (file.recordNumber == recordNumber) {
			return std::move(file.content);
		}
	}
	return {};
}

// A deleted file whose `$DATA` has a hole in it. Sparse runs are decoded
// faithfully but refused by the extent mapper (story-0012), so this is the
// record that must come back with no extents rather than with wrong ones.
[[nodiscard]] std::vector<std::byte> sparseFileRecord() {
	const std::vector<DataRun> runs{
		DataRun{.startCluster = 70, .lengthClusters = 1, .sparse = false},
		DataRun{.startCluster = 0, .lengthClusters = 1, .sparse = true}};
	const std::vector<std::vector<std::byte>> parts{
		buildStandardInformation(),
		buildFileName(
			FileNameSpec{
				.parentRecord = kRootRecord,
				.parentSequence = 1,
				.name = "sparse.bin",
				.realSize = 8192}),
		buildNonResidentData(
			NonResidentDataSpec{
				.runs = runs,
				.realSize = 8192,
				.bytesPerCluster = makeLayout().bytesPerCluster()}),
		buildEndMarker()};
	std::vector<std::byte> attributes;
	for (const auto& part : parts) {
		attributes.insert(attributes.end(), part.begin(), part.end());
	}
	return buildMftRecord(
		makeLayout(),
		MftRecordSpec{
			.sequence = 1,
			.inUse = false,
			.isDirectory = false,
			.attributes = attributes});
}

[[nodiscard]] std::uint64_t totalExtentBytes(const std::vector<Extent>& extents) {
	return std::accumulate(
		extents.begin(),
		extents.end(),
		std::uint64_t{0},
		[](std::uint64_t sum, const Extent& extent) { return sum + extent.lengthBytes; });
}

// One enumerated fixture volume, so a test states only the damage it cares
// about and then asks what came out.
class NtfsEntryEnumeration : public ::testing::Test {
protected:
	[[nodiscard]] NtfsVolume& volume() {
		return volume_;
	}

	// Runs the enumeration; the entries are a convenience, not the whole point,
	// so a test may care only about the stats it left behind.
	const std::vector<RecoveredEntry>& enumerate() {
		walkVolume();
		return visitor_.entries();
	}

	void walkVolume() {
		const auto table = volume_.openTable();
		ASSERT_TRUE(table.hasValue());
		stats_ = enumerateEntries(table.value(), visitor_);
	}

	[[nodiscard]] const Result<revenant::fs::ntfs::EnumerationStats>& stats() const {
		return stats_;
	}

private:
	NtfsVolume volume_;
	CollectingEntryVisitor visitor_;
	Result<revenant::fs::ntfs::EnumerationStats> stats_{
		Error{.code = ErrorCode::kNotFound, .offset = 0, .osCode = 0}};
};

TEST_F(NtfsEntryEnumeration, ReportsEveryUserFileWithItsPathStateAndGrade) {
	const std::vector<EntrySummary> expected{
		EntrySummary{
			.path = "photos/keep.jpg",
			.state = EntryState::kLive,
			.recoverability = Confidence::kValid},
		EntrySummary{
			.path = "photos/deleted.jpg",
			.state = EntryState::kDeleted,
			.recoverability = Confidence::kValid},
		EntrySummary{
			.path = "notes.txt",
			.state = EntryState::kDeleted,
			.recoverability = Confidence::kValid},
		EntrySummary{
			.path = "orphan.jpg",
			.state = EntryState::kOrphaned,
			.recoverability = Confidence::kUncertain}};
	EXPECT_EQ(summarize(enumerate()), expected);
}

// Directories carry no content, and records 0-15 are the filesystem's own
// bookkeeping — neither is something a user asked to get back.
TEST_F(NtfsEntryEnumeration, ReportsNeitherDirectoriesNorMetadataFiles) {
	const auto& entries = enumerate();
	EXPECT_EQ(entryNamed(entries, "photos"), nullptr);
	EXPECT_EQ(entryNamed(entries, "$MFT"), nullptr);
	EXPECT_EQ(entryNamed(entries, "."), nullptr);
}

TEST_F(NtfsEntryEnumeration, CountsEveryUserSlotItWalked) {
	enumerate();
	ASSERT_TRUE(stats().hasValue());
	EXPECT_EQ(stats().value().recordsScanned, makeLayout().mftRecordCount - kFirstUserRecord);
	EXPECT_EQ(stats().value().entriesReported, 4U);
}

// An empty or destroyed slot is exactly what the carve pass exists for; it must
// not stop the walk over the slots that are still readable.
TEST_F(NtfsEntryEnumeration, SkipsARecordSlotThatWillNotParse) {
	volume().putRecord(kKeepJpegRecord, std::vector<std::byte>(makeLayout().mftRecordBytes));
	const auto& entries = enumerate();
	EXPECT_EQ(entries.size(), 3U);
	EXPECT_EQ(entryNamed(entries, "photos/keep.jpg"), nullptr);
	EXPECT_NE(entryNamed(entries, "photos/deleted.jpg"), nullptr);
}

TEST_F(NtfsEntryEnumeration, CarriesTheResidentContentOfASmallFileInline) {
	const auto* entry = entryNamed(enumerate(), "notes.txt");
	ASSERT_NE(entry, nullptr);
	const auto expected = fixtureContent(kDeletedNotesRecord);
	EXPECT_EQ(entry->residentContent, expected);
	EXPECT_TRUE(entry->extents.empty());
	EXPECT_EQ(entry->sizeInBytes, expected.size());
}

TEST_F(NtfsEntryEnumeration, CarriesTheExtentsOfAFragmentedFile) {
	const auto* entry = entryNamed(enumerate(), "photos/deleted.jpg");
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(entry->extents.size(), 2U);
	EXPECT_TRUE(entry->residentContent.empty());
	EXPECT_EQ(entry->sizeInBytes, fixtureContent(kDeletedJpegRecord).size());
	EXPECT_EQ(totalExtentBytes(entry->extents), entry->sizeInBytes);
}

TEST_F(NtfsEntryEnumeration, CarriesTheRecordsTimestamps) {
	const auto* entry = entryNamed(enumerate(), "notes.txt");
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(entry->timestamps.created, kFixtureCreated);
}

// Approximating a sparse file's bytes would hand back the wrong content, so the
// entry keeps its name and loses its extents: uncertain, and carve territory.
TEST_F(NtfsEntryEnumeration, ReportsASparseFileWithNoExtentsAndAnUncertainGrade) {
	volume().putRecord(kKeepJpegRecord, sparseFileRecord());
	const auto* entry = entryNamed(enumerate(), "sparse.bin");
	ASSERT_NE(entry, nullptr);
	EXPECT_TRUE(entry->extents.empty());
	EXPECT_TRUE(entry->residentContent.empty());
	EXPECT_EQ(entry->recoverability, Confidence::kUncertain);
	EXPECT_EQ(entry->state, EntryState::kDeleted);
}

// A disk that will not read is not a disk with no files on it.
TEST(NtfsEntryEnumerationFault, PropagatesADeviceReadFault) {
	NtfsVolume volume;
	const auto image = volume.bytes();
	FaultingDevice device{
		std::vector<std::byte>{image.begin(), image.end()},
		revenant::testing::recordOffset(kFirstUserRecord)};
	const auto table = MftTable::open(device, volume.geometry());
	ASSERT_TRUE(table.hasValue());
	CollectingEntryVisitor visitor;
	const auto stats = enumerateEntries(table.value(), visitor);
	ASSERT_FALSE(stats.hasValue());
	EXPECT_EQ(stats.error().code, ErrorCode::kIoFailure);
}

} // namespace
