// SPDX-License-Identifier: GPL-3.0-or-later
// story-0405: what `--list-partitions` actually prints. The offsets are the
// point: this output is as often piped into the next command as read, so a
// rounded number would be the wrong kind of helpful.
#include "cli/PartitionListing.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

#include "imagegen/disk/DiskImageBuilder.hpp"
#include "support/TempFile.hpp"

namespace {

using revenant::cli::describePartitions;
using revenant::imagegen::disk::buildMbrDiskImage;
using revenant::testing::TempFile;

// An empty result means the source would not open, which every test here
// asserts against by expecting lines.
[[nodiscard]] std::vector<std::string> linesOf(const std::vector<std::byte>& image) {
	const TempFile file{image};
	const auto lines = describePartitions(file.path());
	if (!lines.hasValue()) {
		return {};
	}
	return lines.value();
}

TEST(PartitionListing, HeadsTheListWithTheSchemeAndTheCount) {
	const auto lines = linesOf(buildMbrDiskImage().bytes);
	ASSERT_FALSE(lines.empty());
	EXPECT_EQ(lines.front(), std::string{"partitions: MBR, 4 found"});
}

TEST(PartitionListing, GivesOneLinePerPartitionWithItsOffsetAndLabel) {
	const auto disk = buildMbrDiskImage();
	const auto lines = linesOf(disk.bytes);
	ASSERT_EQ(lines.size(), 5U);
	EXPECT_TRUE(lines.at(1).starts_with("  1: offset " + std::to_string(disk.volumeOffsets.at(0))));
	EXPECT_TRUE(lines.at(1).ends_with("NTFS/exFAT"));
}

TEST(PartitionListing, NamesEachFilesystemsTypeByte) {
	const auto lines = linesOf(buildMbrDiskImage().bytes);
	ASSERT_EQ(lines.size(), 5U);
	EXPECT_TRUE(lines.at(2).ends_with("FAT32 (LBA)"));
	EXPECT_TRUE(lines.at(4).ends_with("Linux"));
}

// A source with no table is a single volume, and saying so is the answer to the
// question rather than a complaint about it.
TEST(PartitionListing, AnUnpartitionedSourceIsListedAsASingleVolume) {
	const std::vector<std::byte> blank(4096, std::byte{0});
	const auto lines = linesOf(blank);
	ASSERT_EQ(lines.size(), 1U);
	EXPECT_TRUE(lines.front().starts_with("partitions: none;"));
}

TEST(PartitionListing, ASourceThatCannotBeOpenedIsATypedError) {
	EXPECT_FALSE(describePartitions("no-such-image.img").hasValue());
}

} // namespace
