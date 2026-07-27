// SPDX-License-Identifier: GPL-3.0-or-later
// The story-0065 proof: everything asserted here goes through the production
// parsers reading the generated image as a real device, never through the
// builder's own types. Fallible steps go through `Result::value()`, which
// throws on an error — a loud test failure, and one less branch per helper.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "imagegen/ntfs/FixtureFiles.hpp"
#include "imagegen/ntfs/NtfsImageBuilder.hpp"
#include "imagegen/ntfs/NtfsLayout.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/ImageFileDevice.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/ntfs/BootSector.hpp"
#include "revenant/fs/ntfs/MftRecord.hpp"
#include "revenant/fs/ntfs/Runlist.hpp"
#include "support/TempFile.hpp"

namespace {

using revenant::Confidence;
using revenant::ErrorCode;
using revenant::ImageFileDevice;
using revenant::Result;
using revenant::fs::Extent;
using revenant::fs::ntfs::decodeRunlist;
using revenant::fs::ntfs::MftData;
using revenant::fs::ntfs::MftRecordView;
using revenant::fs::ntfs::NtfsGeometry;
using revenant::fs::ntfs::parseBootSector;
using revenant::fs::ntfs::parseMftRecord;
using revenant::fs::ntfs::runlistExtents;
using revenant::imagegen::ntfs::buildNtfsImage;
using revenant::imagegen::ntfs::FixtureFile;
using revenant::imagegen::ntfs::fixtureFiles;
using revenant::imagegen::ntfs::kDeletedJpegRecord;
using revenant::imagegen::ntfs::kDeletedNotesRecord;
using revenant::imagegen::ntfs::kKeepJpegRecord;
using revenant::imagegen::ntfs::kMftRecord;
using revenant::imagegen::ntfs::kOrphanJpegRecord;
using revenant::imagegen::ntfs::kPhotosRecord;
using revenant::imagegen::ntfs::kRootRecord;
using revenant::imagegen::ntfs::kUnallocatedJpegCluster;
using revenant::imagegen::ntfs::makeLayout;
using revenant::imagegen::ntfs::unallocatedJpeg;
using revenant::testing::TempFile;

[[nodiscard]] std::unique_ptr<ImageFileDevice> openDevice(const TempFile& file) {
	return std::move(ImageFileDevice::open(file.path()).value());
}

[[nodiscard]] FixtureFile fixture(std::uint64_t recordNumber) {
	for (auto& file : fixtureFiles(makeLayout())) {
		if (file.recordNumber == recordNumber) {
			return std::move(file);
		}
	}
	return FixtureFile{};
}

// The record's `$DATA`. std::optional::value() throws when it is absent, which
// is exactly the loud failure this test wants; the analyzer cannot see that.
[[nodiscard]] const MftData& dataOf(const MftRecordView& view) {
	// NOLINTNEXTLINE(bugprone-unchecked-optional-access) - value() throws if absent.
	return view.data.value();
}

// What a directory entry amounts to, in one comparable value: asserting on the
// whole thing at once gives one readable diff instead of six separate failures.
struct EntrySummary {
	Confidence grade{};
	bool inUse{};
	bool isDirectory{};
	std::string name;
	std::uint64_t parentRecord{};

	friend bool operator==(const EntrySummary&, const EntrySummary&) = default;
};

std::ostream& operator<<(std::ostream& out, const EntrySummary& summary) {
	return out << (summary.isDirectory ? "dir " : "file ") << summary.name << " parent "
			   << summary.parentRecord << (summary.inUse ? " (live)" : " (deleted)") << " grade "
			   << static_cast<int>(summary.grade);
}

[[nodiscard]] EntrySummary summarize(const MftRecordView& view) {
	return EntrySummary{
		.grade = view.grade,
		.inUse = view.inUse,
		.isDirectory = view.isDirectory,
		.name = view.names.empty() ? std::string{"<no name>"} : view.names.front().name.utf8,
		.parentRecord = view.names.empty() ? 0 : view.names.front().parentRecord};
}

// Every fixture record is expected to parse cleanly, so the grade belongs in
// the expectation rather than in a separate assertion.
[[nodiscard]] EntrySummary summarize(const FixtureFile& file) {
	return EntrySummary{
		.grade = Confidence::kValid,
		.inUse = file.inUse,
		.isDirectory = file.isDirectory,
		.name = std::string{file.name},
		.parentRecord = file.parentRecord};
}

// One mounted fixture image, opened the way the real tools open a source.
class NtfsImage : public ::testing::Test {
protected:
	NtfsImage() : image_(buildNtfsImage()), file_(image_), device_(openDevice(file_)) {}

	[[nodiscard]] std::vector<std::byte> read(const Extent& extent) {
		std::vector<std::byte> buffer(static_cast<std::size_t>(extent.lengthBytes), std::byte{0});
		EXPECT_TRUE(device_->readAt(extent.deviceOffset, buffer).hasValue());
		return buffer;
	}

	[[nodiscard]] std::vector<std::byte> readRecordBytes(std::uint64_t recordNumber) {
		const auto layout = makeLayout();
		return read(
			Extent{
				.deviceOffset = layout.mftOffsetBytes() + (recordNumber * layout.mftRecordBytes),
				.lengthBytes = layout.mftRecordBytes});
	}

	[[nodiscard]] Result<MftRecordView> parseRecord(std::uint64_t recordNumber) {
		return parseMftRecord(readRecordBytes(recordNumber), recordNumber);
	}

	[[nodiscard]] NtfsGeometry geometry() {
		return parseBootSector(
				   read(Extent{.deviceOffset = 0, .lengthBytes = makeLayout().bytesPerSector}))
			.value();
	}

	[[nodiscard]] std::vector<std::byte> readExtents(std::span<const Extent> extents) {
		std::vector<std::byte> content;
		for (const auto& extent : extents) {
			const auto chunk = read(extent);
			content.insert(content.end(), chunk.begin(), chunk.end());
		}
		return content;
	}

	// Reads a non-resident file's content the way `revenant-undelete` will:
	// runlist → extents → device reads, concatenated in file order.
	[[nodiscard]] std::vector<std::byte>
	readThroughRunlist(std::span<const std::byte> runlistBytes, std::uint64_t realSize) {
		const auto runlist = decodeRunlist(runlistBytes);
		const auto extents = runlistExtents(runlist.value(), geometry(), realSize);
		return readExtents(extents.value());
	}

	// One fixture row's record, checked against what the table says it is.
	void expectRecordMatchesFixture(const FixtureFile& file) {
		const auto parsed = parseRecord(file.recordNumber);
		EXPECT_EQ(summarize(parsed.value()), summarize(file));
	}

	// A non-resident file's bytes, recovered through its own metadata alone.
	void expectContentRecoverable(std::uint64_t recordNumber) {
		const auto expected = fixture(recordNumber).content;
		const auto parsed = parseRecord(recordNumber);
		const auto& data = dataOf(parsed.value());
		EXPECT_EQ(readThroughRunlist(data.runlistBytes, expected.size()), expected);
	}

	[[nodiscard]] ImageFileDevice& device() {
		return *device_;
	}

private:
	std::vector<std::byte> image_;
	TempFile file_;
	std::unique_ptr<ImageFileDevice> device_;
};

TEST_F(NtfsImage, BootSectorParsesBackAsTheLayoutGeometry) {
	const auto layout = makeLayout();
	const auto parsed = geometry();
	EXPECT_EQ(parsed.bytesPerCluster, layout.bytesPerCluster());
	EXPECT_EQ(parsed.totalClusters, layout.totalClusters);
	EXPECT_EQ(parsed.mftOffsetBytes, layout.mftOffsetBytes());
	EXPECT_EQ(parsed.bytesPerMftRecord, layout.mftRecordBytes);
}

TEST_F(NtfsImage, TheImageIsExactlyTheVolumeSize) {
	EXPECT_EQ(device().sizeInBytes(), makeLayout().totalBytes());
}

TEST_F(NtfsImage, EveryFixtureRecordMatchesTheFixtureTable) {
	for (const auto& file : fixtureFiles(makeLayout())) {
		SCOPED_TRACE(file.name);
		expectRecordMatchesFixture(file);
	}
}

TEST_F(NtfsImage, TheMftDescribesItselfAtItsRealSize) {
	const auto layout = makeLayout();
	const auto parsed = parseRecord(kMftRecord);
	ASSERT_TRUE(parsed.hasValue());
	const auto& data = dataOf(parsed.value());
	EXPECT_FALSE(data.resident);
	EXPECT_EQ(data.realSize, std::uint64_t{layout.mftRecordCount} * layout.mftRecordBytes);
	EXPECT_EQ(decodeRunlist(data.runlistBytes).value().totalClusters, layout.mftClusterCount());
}

TEST_F(NtfsImage, UnusedRecordSlotsAreSkippable) {
	// Record 1 holds no `FILE` signature: an enumerator must be able to step
	// over it rather than treat the slot as damaged metadata.
	const auto parsed = parseRecord(1);
	ASSERT_FALSE(parsed.hasValue());
	EXPECT_EQ(parsed.error().code, ErrorCode::kNotFound);
}

TEST_F(NtfsImage, TheDeletedJpegIsFragmentedAcrossTwoRuns) {
	const auto parsed = parseRecord(kDeletedJpegRecord);
	ASSERT_TRUE(parsed.hasValue());
	EXPECT_FALSE(parsed.value().inUse);
	const auto& data = dataOf(parsed.value());
	EXPECT_FALSE(data.resident);
	EXPECT_EQ(decodeRunlist(data.runlistBytes).value().runs.size(), 2U);
}

TEST_F(NtfsImage, TheDeletedJpegReadsBackByteIdenticalThroughItsRunlist) {
	expectContentRecoverable(kDeletedJpegRecord);
}

TEST_F(NtfsImage, TheLiveJpegReadsBackByteIdenticalThroughItsRunlist) {
	expectContentRecoverable(kKeepJpegRecord);
}

TEST_F(NtfsImage, TheOrphanJpegReadsBackByteIdenticalThroughItsRunlist) {
	expectContentRecoverable(kOrphanJpegRecord);
}

TEST_F(NtfsImage, TheDeletedResidentFileCarriesItsContentInTheRecord) {
	const auto parsed = parseRecord(kDeletedNotesRecord);
	ASSERT_TRUE(parsed.hasValue());
	const auto& data = dataOf(parsed.value());
	EXPECT_TRUE(data.resident);
	EXPECT_EQ(data.residentContent, fixture(kDeletedNotesRecord).content);
}

TEST_F(NtfsImage, TheOrphanPointsAtAParentThatIsNotThere) {
	const auto parsed = parseRecord(kOrphanJpegRecord);
	ASSERT_TRUE(parsed.hasValue());
	EXPECT_FALSE(parseRecord(parsed.value().names.front().parentRecord).hasValue());
}

TEST_F(NtfsImage, ThePathChainReachesTheRoot) {
	const auto leaf = parseRecord(kKeepJpegRecord);
	ASSERT_TRUE(leaf.hasValue());
	EXPECT_EQ(leaf.value().names.front().parentRecord, kPhotosRecord);
	const auto directory = parseRecord(kPhotosRecord);
	ASSERT_TRUE(directory.hasValue());
	EXPECT_TRUE(directory.value().isDirectory);
	EXPECT_EQ(directory.value().names.front().parentRecord, kRootRecord);
}

TEST_F(NtfsImage, AJpegSitsInUnallocatedSpaceWhereNoRecordPointsAtIt) {
	const auto expected = unallocatedJpeg();
	EXPECT_EQ(
		read(
			Extent{
				.deviceOffset = makeLayout().clusterOffsetBytes(kUnallocatedJpegCluster),
				.lengthBytes = expected.size()}),
		expected);
}

TEST(NtfsImageGeneration, IsDeterministic) {
	EXPECT_EQ(buildNtfsImage(), buildNtfsImage());
}

} // namespace
