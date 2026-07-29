// SPDX-License-Identifier: GPL-3.0-or-later
// story-0307: ext4 arriving at the shared front door. The volume itself is
// proven end to end by the integration test; what is asked here is the mount
// table's own rule — a filesystem that finds its signature *owns* the answer,
// and one that does not passes the volume on.
#include "revenant/fs/Mount.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "imagegen/ByteWriter.hpp"
#include "imagegen/ext4/Ext4ImageBuilder.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/fs/ext4/Superblock.hpp"
#include "support/InMemoryDevice.hpp"

namespace {

using revenant::ErrorCode;
using revenant::fs::mountVolume;
using revenant::fs::ext4::kSuperblockOffset;
using revenant::imagegen::putLe;
using revenant::imagegen::ext4::buildExt4Image;
using revenant::testing::InMemoryDevice;

constexpr std::uint32_t kSectorSize = 512;

[[nodiscard]] ErrorCode mountErrorOf(std::vector<std::byte> image) {
	InMemoryDevice device{std::move(image), kSectorSize};
	const auto mounted = mountVolume(device);
	EXPECT_FALSE(mounted.hasValue());
	return mounted.hasValue() ? ErrorCode::kNotFound : mounted.error().code;
}

void writeSuperblockField(std::vector<std::byte>& image, std::size_t offset, std::uint16_t value) {
	putLe<std::uint16_t>(image, static_cast<std::size_t>(kSuperblockOffset) + offset, value);
}

// NTFS, exFAT and FAT32 all decline it — none of them finds its signature in
// sector 0 — and ext4, asked last, does not.
TEST(Ext4Mount, TheVolumeMountsThroughTheSharedFrontDoor) {
	InMemoryDevice device{buildExt4Image(), kSectorSize};
	EXPECT_TRUE(mountVolume(device).hasValue());
}

// A corrupt ext4 volume is not an unknown volume. ext4 recognized it, so ext4's
// own typed error is the answer, and the run is not sent hunting for a FAT that
// was never there.
TEST(Ext4Mount, ARecognizedButBrokenVolumeKeepsItsOwnError) {
	auto image = buildExt4Image();
	writeSuperblockField(image, 0x58, 200); // an inode size that is not a power of two
	EXPECT_EQ(mountErrorOf(std::move(image)), ErrorCode::kInvalidArgument);
}

// Sixteen bits of magic a kilobyte in is the weakest signature of the four, so
// the block size beside it is part of the recognition: without both, ext4 hands
// the volume back rather than claiming it.
TEST(Ext4Mount, AVolumeWhoseBlockSizeExtFourCannotExpressIsDeclinedRatherThanClaimed) {
	auto image = buildExt4Image();
	putLe<std::uint32_t>(image, static_cast<std::size_t>(kSuperblockOffset) + 0x18, 9);
	EXPECT_EQ(mountErrorOf(std::move(image)), ErrorCode::kNotFound);
}

TEST(Ext4Mount, AVolumeWithNoExtFourMagicIsNotClaimed) {
	auto image = buildExt4Image();
	writeSuperblockField(image, 0x38, 0);
	EXPECT_EQ(mountErrorOf(std::move(image)), ErrorCode::kNotFound);
}

} // namespace
