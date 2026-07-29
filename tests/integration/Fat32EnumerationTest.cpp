// SPDX-License-Identifier: GPL-3.0-or-later
// The story-0303 proof: the synthetic FAT32 volume mounted through the same
// front door the real tools use — fs::mountVolume — hands back every file with
// its name, its place in the tree, and extents that really do hold its bytes.
// A live fragmented file comes back exactly; a deleted one comes back on the
// contiguity guess, and says it is a guess.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "imagegen/fat/Fat32Fixture.hpp"
#include "imagegen/fat/Fat32ImageBuilder.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/fs/FileSystem.hpp"
#include "revenant/fs/Mount.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/Types.hpp"
#include "support/CollectingEntryVisitor.hpp"
#include "support/InMemoryDevice.hpp"

namespace {

using revenant::Confidence;
using revenant::fs::EntryState;
using revenant::fs::Extent;
using revenant::fs::mountVolume;
using revenant::fs::RecoveredEntry;
using revenant::imagegen::fat::buildFat32Image;
using revenant::imagegen::fat::fat32FixtureFiles;
using revenant::testing::CollectingEntryVisitor;
using revenant::testing::InMemoryDevice;

constexpr std::uint32_t kSectorSize = 512;

// The fixture volume, mounted the way the real tools mount a source, and
// enumerated once for the whole suite's worth of questions.
class Fat32Enumeration : public ::testing::Test {
protected:
	Fat32Enumeration() : image_(buildFat32Image()), device_(image_, kSectorSize) {
		mountAndEnumerate();
	}

	[[nodiscard]] const std::vector<RecoveredEntry>& entries() const {
		return visitor_.entries();
	}

	[[nodiscard]] const RecoveredEntry* entryAt(std::string_view path) const {
		const auto found = std::ranges::find(entries(), path, &RecoveredEntry::path);
		return found != entries().end() ? &*found : nullptr;
	}

	// An entry's bytes, fetched the way `revenant-undelete` will fetch them:
	// read back off the device through the extents the entry reported.
	[[nodiscard]] std::vector<std::byte> contentOf(const RecoveredEntry& entry) {
		std::vector<std::byte> content;
		for (const Extent& extent : entry.extents) {
			const auto chunk = read(extent);
			content.insert(content.end(), chunk.begin(), chunk.end());
		}
		return content;
	}

private:
	[[nodiscard]] std::vector<std::byte> read(const Extent& extent) {
		std::vector<std::byte> chunk(static_cast<std::size_t>(extent.lengthBytes), std::byte{0});
		EXPECT_TRUE(device_.readAt(extent.deviceOffset, chunk).hasValue());
		return chunk;
	}

	void walk(const revenant::fs::FileSystem& volume) {
		EXPECT_TRUE(volume.enumerate(visitor_).hasValue());
	}

	void mountAndEnumerate() {
		const auto mounted = mountVolume(device_);
		ASSERT_TRUE(mounted.hasValue());
		walk(*mounted.value());
	}

	std::vector<std::byte> image_;
	InMemoryDevice device_;
	CollectingEntryVisitor visitor_;
};

// What the fixture table says a file of that short name holds.
[[nodiscard]] std::vector<std::byte> fixtureContentNamed(std::string_view shortName) {
	for (const auto& file : fat32FixtureFiles()) {
		if (file.shortName == shortName) {
			return file.content;
		}
	}
	return {};
}

TEST_F(Fat32Enumeration, ReportsEveryFileOnTheVolumeAndNoDirectories) {
	EXPECT_EQ(entries().size(), 5U);
}

// The long name is the one a user would recognize; the 8.3 name is what the
// format had to store alongside it.
TEST_F(Fat32Enumeration, GivesALiveFileItsLongNameAndItsPlaceInTheTree) {
	const auto* entry = entryAt("photos/keep-photo.jpg");
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(entry->state, EntryState::kLive);
	EXPECT_EQ(entry->recoverability, Confidence::kValid);
}

// Its content lives in two separate places, so recovering it byte-for-byte is
// what proves the chain was followed rather than assumed contiguous.
TEST_F(Fat32Enumeration, ALiveFragmentedFileReadsBackExactly) {
	const auto* entry = entryAt("photos/keep-photo.jpg");
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(entry->extents.size(), 2U);
	EXPECT_EQ(contentOf(*entry), fixtureContentNamed("KEEP~1  JPG"));
}

// The deletion marker took the name's first character and nothing else.
TEST_F(Fat32Enumeration, ADeletedFileKeepsAllOfItsNameButTheFirstCharacter) {
	const auto* entry = entryAt("_ELETED.TXT");
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(entry->state, EntryState::kDeleted);
	EXPECT_EQ(entry->recoverability, Confidence::kUncertain);
}

// Its chain was freed, so its extents are the contiguity guess — right here,
// because the fixture laid it out contiguously, and graded as a guess anyway.
TEST_F(Fat32Enumeration, ADeletedFileComesBackOnTheContiguityGuess) {
	const auto* entry = entryAt("_ELETED.TXT");
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(entry->extents.size(), 1U);
	EXPECT_EQ(contentOf(*entry), fixtureContentNamed("DELETED TXT"));
}

// The file inside the deleted directory: its name survived, its place in the
// tree did not, so it is an orphan rather than merely deleted.
TEST_F(Fat32Enumeration, AFileUnderADeletedDirectoryIsOrphaned) {
	const auto* entry = entryAt("_ONE/_RPHAN.JPG");
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(entry->state, EntryState::kOrphaned);
	EXPECT_EQ(entry->recoverability, Confidence::kUncertain);
	EXPECT_EQ(contentOf(*entry), fixtureContentNamed("ORPHAN  JPG"));
}

TEST_F(Fat32Enumeration, EveryEntryReportsExactlyItsDeclaredSize) {
	for (const auto& entry : entries()) {
		SCOPED_TRACE(entry.path);
		EXPECT_EQ(contentOf(entry).size(), entry.sizeInBytes);
	}
}

// `.` and `..` are the format's own bookkeeping and name no file.
TEST_F(Fat32Enumeration, TheDotEntriesReportNothing) {
	EXPECT_EQ(entryAt("photos/."), nullptr);
	EXPECT_EQ(entryAt("photos/.."), nullptr);
}

} // namespace
