// SPDX-License-Identifier: GPL-3.0-or-later
// The story-0013 proof: the fixture volume opened as a real device file and
// walked with the production stack — parseBootSector -> MftTable ->
// enumerateEntries — hands back every user file with its name, its place in
// the tree, and extents that really do hold its bytes.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "imagegen/ntfs/FixtureFiles.hpp"
#include "imagegen/ntfs/NtfsImageBuilder.hpp"
#include "imagegen/ntfs/NtfsLayout.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/io/ImageFileDevice.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/ntfs/BootSector.hpp"
#include "revenant/fs/ntfs/EntryEnumeration.hpp"
#include "revenant/fs/ntfs/MftTable.hpp"
#include "support/CollectingEntryVisitor.hpp"
#include "support/TempFile.hpp"

namespace {

using revenant::Confidence;
using revenant::ImageFileDevice;
using revenant::fs::EntryState;
using revenant::fs::Extent;
using revenant::fs::RecoveredEntry;
using revenant::fs::ntfs::enumerateEntries;
using revenant::fs::ntfs::MftTable;
using revenant::fs::ntfs::parseBootSector;
using revenant::imagegen::ntfs::buildNtfsImage;
using revenant::imagegen::ntfs::fixtureFiles;
using revenant::imagegen::ntfs::kUnallocatedJpegCluster;
using revenant::imagegen::ntfs::makeLayout;
using revenant::testing::CollectingEntryVisitor;
using revenant::testing::TempFile;

[[nodiscard]] std::unique_ptr<ImageFileDevice> openDevice(const TempFile& file) {
	return std::move(ImageFileDevice::open(file.path()).value());
}

[[nodiscard]] std::string_view leafOf(std::string_view path) {
	const auto slash = path.rfind('/');
	return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

// What the fixture table says the file of that name holds — the expectation, so
// the test never restates content of its own.
[[nodiscard]] std::vector<std::byte> fixtureContentNamed(std::string_view name) {
	for (auto& file : fixtureFiles(makeLayout())) {
		if (file.name == name) {
			return std::move(file.content);
		}
	}
	return {};
}

[[nodiscard]] bool coversCluster(const Extent& extent, std::uint64_t cluster) {
	const auto offset = makeLayout().clusterOffsetBytes(cluster);
	return extent.deviceOffset <= offset && offset < extent.deviceOffset + extent.lengthBytes;
}

// The fixture volume, mounted the way the real tools mount a source, and
// enumerated once for the whole suite's worth of questions.
class NtfsEnumeration : public ::testing::Test {
protected:
	NtfsEnumeration() : file_(buildNtfsImage()), device_(openDevice(file_)) {
		mountAndEnumerate();
	}

	[[nodiscard]] const std::vector<RecoveredEntry>& entries() const {
		return visitor_.entries();
	}

	// Every extent every entry claims, flattened — what the volume-wide
	// questions below are actually about.
	[[nodiscard]] std::vector<Extent> allExtents() const {
		std::vector<Extent> extents;
		for (const auto& entry : entries()) {
			extents.insert(extents.end(), entry.extents.begin(), entry.extents.end());
		}
		return extents;
	}

	[[nodiscard]] const RecoveredEntry* entryAt(std::string_view path) const {
		const auto found = std::ranges::find(entries(), path, &RecoveredEntry::path);
		return found != entries().end() ? &*found : nullptr;
	}

	// An entry's bytes, fetched the way `revenant-undelete` will fetch them:
	// resident content as parsed, everything else read back off the device
	// through the extents the entry reported.
	[[nodiscard]] std::vector<std::byte> contentOf(const RecoveredEntry& entry) {
		std::vector<std::byte> content = entry.residentContent;
		for (const auto& extent : entry.extents) {
			const auto chunk = read(extent);
			content.insert(content.end(), chunk.begin(), chunk.end());
		}
		return content;
	}

	[[nodiscard]] std::uint64_t deviceSize() const {
		return device_->sizeInBytes();
	}

private:
	[[nodiscard]] std::vector<std::byte> read(const Extent& extent) {
		std::vector<std::byte> chunk(static_cast<std::size_t>(extent.lengthBytes), std::byte{0});
		EXPECT_TRUE(device_->readAt(extent.deviceOffset, chunk).hasValue());
		return chunk;
	}

	[[nodiscard]] revenant::fs::ntfs::NtfsGeometry geometry() {
		std::vector<std::byte> sector(makeLayout().bytesPerSector, std::byte{0});
		EXPECT_TRUE(device_->readAt(0, sector).hasValue());
		return parseBootSector(sector).value();
	}

	void mountAndEnumerate() {
		const auto table = MftTable::open(*device_, geometry());
		ASSERT_TRUE(table.hasValue());
		walk(table.value());
	}

	void walk(const MftTable& table) {
		EXPECT_TRUE(enumerateEntries(table, visitor_).hasValue());
	}

	TempFile file_;
	std::unique_ptr<ImageFileDevice> device_;
	CollectingEntryVisitor visitor_;
};

TEST_F(NtfsEnumeration, RecoversEveryNamedUserFileOnTheVolume) {
	EXPECT_EQ(entries().size(), 4U);
}

TEST_F(NtfsEnumeration, GivesTheDeletedFileBackItsNameAndItsPlaceInTheTree) {
	const auto* entry = entryAt("photos/deleted.jpg");
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(entry->state, EntryState::kDeleted);
	EXPECT_EQ(entry->recoverability, Confidence::kValid);
	EXPECT_EQ(entry->extents.size(), 2U);
}

// The orphan keeps its name and its bytes; only its place in the tree is lost.
TEST_F(NtfsEnumeration, ReportsTheOrphanRatherThanDroppingIt) {
	const auto* entry = entryAt("orphan.jpg");
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(entry->state, EntryState::kOrphaned);
	EXPECT_EQ(entry->recoverability, Confidence::kUncertain);
	EXPECT_FALSE(entry->extents.empty());
}

TEST_F(NtfsEnumeration, EveryEntryReadsBackByteIdenticalToTheFixture) {
	for (const auto& entry : entries()) {
		SCOPED_TRACE(entry.path);
		EXPECT_EQ(contentOf(entry), fixtureContentNamed(leafOf(entry.path)));
	}
}

TEST_F(NtfsEnumeration, EveryEntryReportsExactlyItsDeclaredSize) {
	for (const auto& entry : entries()) {
		SCOPED_TRACE(entry.path);
		EXPECT_EQ(contentOf(entry).size(), entry.sizeInBytes);
	}
}

TEST_F(NtfsEnumeration, NoEntryPointsOutsideTheVolume) {
	for (const auto& extent : allExtents()) {
		EXPECT_LE(extent.deviceOffset + extent.lengthBytes, deviceSize());
	}
}

// The JPEG in unallocated space is what no record points at. The filesystem
// pass leaving it alone is precisely what makes the carve pass necessary
// (story-0015), so it is asserted here rather than assumed.
TEST_F(NtfsEnumeration, LeavesTheUnallocatedJpegToTheCarvePass) {
	const auto extents = allExtents();
	EXPECT_TRUE(std::ranges::none_of(extents, [](const Extent& extent) {
		return coversCluster(extent, kUnallocatedJpegCluster);
	}));
}

} // namespace
