// SPDX-License-Identifier: GPL-3.0-or-later
// The story-0305 proof: the synthetic exFAT volume mounted through the same
// front door the real tools use, and walked. Three things this asserts that
// FAT32 cannot: a deleted file keeps its whole name, a deleted contiguous file
// states where its bytes are, and a deleted file whose clusters the volume has
// since handed out again gets none.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "imagegen/exfat/ExfatFixture.hpp"
#include "imagegen/exfat/ExfatImageBuilder.hpp"
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
using revenant::imagegen::exfat::buildExfatImage;
using revenant::imagegen::exfat::photosFiles;
using revenant::imagegen::exfat::rootFiles;
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

// What the fixture says a file of that name holds — the expectation, so the
// test never restates content of its own.
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

class ExfatEnumeration : public ::testing::Test {
protected:
	ExfatEnumeration()
		: image_(buildExfatImage()), device_(image_, kSectorSize), entries_(walk(device_)) {}

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

TEST_F(ExfatEnumeration, ReportsEveryFileOnTheVolumeAndNoDirectories) {
	EXPECT_EQ(entryCount(), 4U);
}

// Its content lives in two separate clusters and its set did *not* claim to be
// contiguous, so reading it back exactly proves the table was followed.
TEST_F(ExfatEnumeration, ALiveFragmentedFileReadsBackExactly) {
	const auto* entry = entryAt("keep.txt");
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(entry->state, EntryState::kLive);
	EXPECT_EQ(entry->recoverability, Confidence::kValid);
	EXPECT_EQ(contentOf(*entry), fixtureContentNamed("keep.txt"));
}

TEST_F(ExfatEnumeration, AFileUnderASubdirectoryKeepsItsPlaceInTheTree) {
	const auto* entry = entryAt("photos/inner.bin");
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(contentOf(*entry), fixtureContentNamed("inner.bin"));
}

// exFAT clears one bit per entry and takes no part of the name — which is the
// whole difference from FAT32's `0xE5`.
TEST_F(ExfatEnumeration, ADeletedFileKeepsItsWholeNameAndItsBytes) {
	const auto* entry = entryAt("gone.txt");
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(entry->state, EntryState::kDeleted);
	EXPECT_EQ(entry->recoverability, Confidence::kUncertain);
	EXPECT_EQ(contentOf(*entry), fixtureContentNamed("gone.txt"));
}

// The volume handed this one's cluster out again after it was deleted, so what
// is there now is not what the entry claims. The name is still real, so the
// entry is reported — with no extents, because the region is carve territory
// and handing back a live file's bytes would be worse than handing back none.
TEST_F(ExfatEnumeration, ADeletedFileWhoseClustersWereReusedGetsNoExtents) {
	const auto* entry = entryAt("wiped.txt");
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(entry->state, EntryState::kDeleted);
	EXPECT_TRUE(entry->extents.empty());
	EXPECT_EQ(entry->sizeInBytes, 1000U);
}

} // namespace
