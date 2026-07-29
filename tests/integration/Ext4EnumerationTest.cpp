// SPDX-License-Identifier: GPL-3.0-or-later
// The story-0307 proof: the synthetic ext4 volume mounted through the same front
// door the real tools use, and walked. Four things this asserts that none of the
// other three filesystems can: a deleted name is *found* rather than read, a
// deletion that wiped an inode's extent tree still gives up its bytes through
// the journal, a name whose inode has been handed back out gives up none, and an
// inode nothing names at all still comes back by number.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "imagegen/ext4/Ext4Fixture.hpp"
#include "imagegen/ext4/Ext4ImageBuilder.hpp"
#include "imagegen/ext4/Ext4Layout.hpp"
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
using revenant::imagegen::ext4::buildExt4Image;
using revenant::imagegen::ext4::kOrphanInode;
using revenant::imagegen::ext4::kReusedName;
using revenant::imagegen::ext4::orphanFile;
using revenant::imagegen::ext4::photosFiles;
using revenant::imagegen::ext4::rootFiles;
using revenant::testing::CollectingEntryVisitor;
using revenant::testing::InMemoryDevice;

constexpr std::uint32_t kSectorSize = 512;

[[nodiscard]] std::vector<RecoveredEntry> entriesOf(const revenant::fs::FileSystem& volume) {
	CollectingEntryVisitor visitor;
	EXPECT_TRUE(volume.enumerate(visitor).hasValue());
	return visitor.entries();
}

[[nodiscard]] std::vector<RecoveredEntry> walk(InMemoryDevice& device) {
	const auto mounted = mountVolume(device);
	EXPECT_TRUE(mounted.hasValue());
	return mounted.hasValue() ? entriesOf(*mounted.value()) : std::vector<RecoveredEntry>{};
}

// What the fixture says a file of that name holds — the expectation, so the test
// never restates content of its own.
[[nodiscard]] std::vector<std::byte> fixtureContentNamed(std::string_view name) {
	for (const auto& files : {rootFiles(), photosFiles()}) {
		for (const auto& file : files) {
			if (file.name == name) {
				return file.content;
			}
		}
	}
	return {};
}

class Ext4Enumeration : public ::testing::Test {
protected:
	Ext4Enumeration()
		: image_(buildExt4Image()), device_(image_, kSectorSize), entries_(walk(device_)) {}

	[[nodiscard]] std::size_t entryCount() const {
		return entries_.size();
	}

	[[nodiscard]] const RecoveredEntry* entryAt(std::string_view path) const {
		const auto found = std::ranges::find(entries_, path, &RecoveredEntry::path);
		return found != entries_.end() ? &*found : nullptr;
	}

	// An entry's bytes, fetched the way `revenant-undelete` will fetch them.
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

	std::vector<std::byte> image_;
	InMemoryDevice device_;
	std::vector<RecoveredEntry> entries_;
};

// Two live files in the root, one under `photos`, three deleted names out of the
// holes, and the orphan — and no directory among them.
TEST_F(Ext4Enumeration, ReportsEveryFileOnTheVolumeAndNoDirectories) {
	EXPECT_EQ(entryCount(), 7U);
}

// Its content lives in two runs a long way apart, so reading it back exactly
// proves the extent tree was followed rather than its start assumed.
TEST_F(Ext4Enumeration, ALiveFragmentedFileReadsBackExactly) {
	const auto* entry = entryAt("keep.txt");
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(entry->state, EntryState::kLive);
	EXPECT_EQ(entry->recoverability, Confidence::kValid);
	EXPECT_EQ(contentOf(*entry), fixtureContentNamed("keep.txt"));
}

TEST_F(Ext4Enumeration, AFileUnderASubdirectoryKeepsItsPlaceInTheTree) {
	const auto* entry = entryAt("photos/inner.bin");
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(contentOf(*entry), fixtureContentNamed("inner.bin"));
}

// Nothing on the volume points at this name any more: its record was swallowed
// by the one before it, and the walk had to search the hole to find it.
TEST_F(Ext4Enumeration, ADeletedFileIsFoundInTheHoleItsNeighbourSwallowed) {
	const auto* entry = entryAt("gone.txt");
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(entry->state, EntryState::kDeleted);
	EXPECT_EQ(entry->recoverability, Confidence::kUncertain);
	EXPECT_EQ(contentOf(*entry), fixtureContentNamed("gone.txt"));
}

// The deletion zeroed this inode's extent tree, so nothing *on disk* says where
// its bytes are. The journal still holds the inode table block as it stood
// before that transaction, and that copy still maps the file.
TEST_F(Ext4Enumeration, AWipedExtentTreeComesBackThroughTheJournal) {
	const auto* entry = entryAt("wiped.txt");
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(entry->state, EntryState::kDeleted);
	EXPECT_FALSE(entry->extents.empty());
	EXPECT_EQ(contentOf(*entry), fixtureContentNamed("wiped.txt"));
}

// The name is real; the inode behind it has links again, so something else owns
// those blocks now. Handing back a live file's bytes would be worse than
// handing back none, and the region is what the carve pass is for.
TEST_F(Ext4Enumeration, ADeletedNameWhoseInodeWasReusedGetsNoExtents) {
	const auto* entry = entryAt(kReusedName);
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(entry->state, EntryState::kDeleted);
	EXPECT_TRUE(entry->extents.empty());
}

// Unlinked while still open: no directory entry names it anywhere, so there is
// no name to recover — only its number and its content.
TEST_F(Ext4Enumeration, AnInodeOnTheOrphanListComesBackByNumber) {
	const auto* entry = entryAt("#" + std::to_string(kOrphanInode));
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(entry->state, EntryState::kOrphaned);
	EXPECT_EQ(entry->recoverability, Confidence::kUncertain);
	EXPECT_EQ(contentOf(*entry), orphanFile().content);
}

TEST_F(Ext4Enumeration, TheDotEntriesAreNeverReportedAsFiles) {
	EXPECT_EQ(entryAt("."), nullptr);
	EXPECT_EQ(entryAt(".."), nullptr);
}

} // namespace
