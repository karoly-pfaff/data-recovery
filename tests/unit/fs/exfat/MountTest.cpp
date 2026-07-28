// SPDX-License-Identifier: GPL-3.0-or-later
// story-0033: an exFAT volume mounted through the front door the real tools
// use, and walked. The deleted file is the point: exFAT clears one bit and
// leaves the whole entry set standing, so its name comes back intact and its
// extent is a stated fact rather than FAT32's contiguity guess.
#include "revenant/fs/Mount.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "revenant/core/Confidence.hpp"
#include "revenant/core/Endian.hpp"
#include "revenant/fs/FileSystem.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/Types.hpp"
#include "support/CollectingEntryVisitor.hpp"
#include "support/InMemoryDevice.hpp"

namespace {

using revenant::Confidence;
using revenant::toLittleEndian;
using revenant::fs::EntryState;
using revenant::fs::mountVolume;
using revenant::fs::RecoveredEntry;
using revenant::testing::CollectingEntryVisitor;
using revenant::testing::InMemoryDevice;

constexpr std::uint32_t kSectorSize = 512;
constexpr std::size_t kImageBytes = std::size_t{512} * 512;
constexpr std::size_t kFatOffset = std::size_t{8} * 512;
constexpr std::size_t kHeapOffset = std::size_t{32} * 512;
constexpr std::size_t kRootOffset = kHeapOffset;
constexpr std::uint32_t kLiveCluster = 10;
constexpr std::uint32_t kDeletedCluster = 20;
constexpr std::uint32_t kEndOfChain = 0x0FFF'FFFF;

void putLe(std::vector<std::byte>& image, std::size_t at, auto value) {
	const auto raw = toLittleEndian<decltype(value)>(value);
	std::ranges::copy(raw, image.begin() + static_cast<std::ptrdiff_t>(at));
}

void putText(std::vector<std::byte>& image, std::size_t at, std::string_view text) {
	std::ranges::transform(text, image.begin() + static_cast<std::ptrdiff_t>(at), [](char c) {
		return static_cast<std::byte>(c);
	});
}

void putBootSector(std::vector<std::byte>& image) {
	putText(image, 0x03, "EXFAT   ");
	putLe(image, 0x48, static_cast<std::uint64_t>(512));
	putLe(image, 0x50, static_cast<std::uint32_t>(8));
	putLe(image, 0x54, static_cast<std::uint32_t>(8));
	putLe(image, 0x58, static_cast<std::uint32_t>(32));
	putLe(image, 0x5C, static_cast<std::uint32_t>(100));
	putLe(image, 0x60, static_cast<std::uint32_t>(2));
	putLe(image, 0x1FE, static_cast<std::uint16_t>(0xAA55U));
}

void putShifts(std::vector<std::byte>& image) {
	putLe(image, 0x6C, static_cast<std::uint8_t>(9));
	putLe(image, 0x6D, static_cast<std::uint8_t>(0));
	putLe(image, 0x6E, static_cast<std::uint8_t>(1));
}

// One entry set: a file entry, a stream extension, and one name fragment.
// `typeMask` is 0x80 for a live set and 0 for a deleted one — the single bit
// exFAT clears.
struct SetSpec {
	std::size_t at;
	std::uint8_t typeMask;
	std::uint32_t cluster;
};

void putStream(std::vector<std::byte>& image, std::size_t at, std::uint32_t cluster) {
	putLe(image, at + 0x01, static_cast<std::uint8_t>(0x02U)); // NoFatChain
	putLe(image, at + 0x03, static_cast<std::uint8_t>(4));     // name length
	putLe(image, at + 0x14, cluster);
	putLe(image, at + 0x18, static_cast<std::uint64_t>(300));
}

void putSet(std::vector<std::byte>& image, const SetSpec& spec) {
	const auto at = spec.at;
	const auto typeMask = spec.typeMask;
	const auto cluster = spec.cluster;
	putLe(image, at, static_cast<std::uint8_t>(0x05U | typeMask));
	putLe(image, at + 0x01, static_cast<std::uint8_t>(2));
	putLe(image, at + 32, static_cast<std::uint8_t>(0x40U | typeMask));
	putStream(image, at + 32, cluster);
	putLe(image, at + 64, static_cast<std::uint8_t>(0x41U | typeMask));
}

void putName(std::vector<std::byte>& image, std::size_t setAt, std::string_view name) {
	for (std::size_t unit = 0; unit < name.size(); ++unit) {
		putLe(image, setAt + 64 + 0x02 + (unit * 2), static_cast<std::uint16_t>(name.at(unit)));
	}
}

[[nodiscard]] std::vector<std::byte> buildVolume() {
	std::vector<std::byte> image(kImageBytes, std::byte{0});
	putBootSector(image);
	putShifts(image);
	putLe(image, kFatOffset + (std::size_t{2} * 4), kEndOfChain);
	putSet(image, SetSpec{.at = kRootOffset, .typeMask = 0x80, .cluster = kLiveCluster});
	putName(image, kRootOffset, "keep");
	putSet(image, SetSpec{.at = kRootOffset + 96, .typeMask = 0x00, .cluster = kDeletedCluster});
	putName(image, kRootOffset + 96, "gone");
	return image;
}

[[nodiscard]] std::vector<RecoveredEntry> entriesOf(const revenant::fs::FileSystem& volume) {
	CollectingEntryVisitor visitor;
	EXPECT_TRUE(volume.enumerate(visitor).hasValue());
	return visitor.entries();
}

[[nodiscard]] std::vector<RecoveredEntry> walkVolume(InMemoryDevice& device) {
	const auto mounted = mountVolume(device);
	EXPECT_TRUE(mounted.hasValue());
	return mounted.hasValue() ? entriesOf(*mounted.value()) : std::vector<RecoveredEntry>{};
}

class ExfatMount : public ::testing::Test {
protected:
	ExfatMount()
		: image_(buildVolume()), device_(image_, kSectorSize), entries_(walkVolume(device_)) {}

	[[nodiscard]] std::size_t entryCount() const {
		return entries_.size();
	}

	[[nodiscard]] const RecoveredEntry* entryAt(std::string_view path) const {
		const auto found = std::ranges::find(entries_, path, &RecoveredEntry::path);
		return found != entries_.end() ? &*found : nullptr;
	}

private:
	std::vector<std::byte> image_;
	InMemoryDevice device_;
	std::vector<RecoveredEntry> entries_;
};

// The mount table asks exFAT before FAT32, because an exFAT volume also carries
// a FAT-shaped boot sector and would otherwise be claimed by the wrong parser.
TEST_F(ExfatMount, TheVolumeMountsThroughTheSharedFrontDoor) {
	EXPECT_EQ(entryCount(), 2U);
}

TEST_F(ExfatMount, ALiveFileComesBackWithItsNameAndItsExtent) {
	const auto* entry = entryAt("keep");
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(entry->state, EntryState::kLive);
	EXPECT_EQ(entry->recoverability, Confidence::kValid);
	EXPECT_EQ(entry->sizeInBytes, 300U);
	ASSERT_EQ(entry->extents.size(), 1U);
	EXPECT_EQ(entry->extents.front().lengthBytes, 300U);
}

// The whole point of exFAT for undelete: the deletion cleared one bit per entry
// and took no part of the name with it.
TEST_F(ExfatMount, ADeletedFileKeepsItsWholeName) {
	const auto* entry = entryAt("gone");
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(entry->state, EntryState::kDeleted);
	EXPECT_EQ(entry->recoverability, Confidence::kUncertain);
}

// Its set said the clusters follow one another, so the extent is stated rather
// than guessed — which is what FAT32 cannot do for a deleted file.
TEST_F(ExfatMount, ADeletedContiguousFileStillStatesWhereItsBytesAre) {
	const auto* entry = entryAt("gone");
	ASSERT_NE(entry, nullptr);
	ASSERT_EQ(entry->extents.size(), 1U);
	EXPECT_EQ(entry->extents.front().lengthBytes, 300U);
	EXPECT_EQ(
		entry->extents.front().deviceOffset,
		kHeapOffset + (std::size_t{kDeletedCluster - 2} * kSectorSize));
}

} // namespace
