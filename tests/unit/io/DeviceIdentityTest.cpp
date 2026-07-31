// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/io/DeviceIdentity.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "support/TempDir.hpp"
#include "support/TempFile.hpp"

namespace {

using revenant::overlaps;
using revenant::StorageExtent;
using revenant::storageOf;
using revenant::storageUnder;
using revenant::testing::TempDir;
using revenant::testing::TempFile;

constexpr std::uint64_t kFirstDisk = 0;
constexpr std::uint64_t kSecondDisk = 1;

constexpr std::uint64_t kGiB = std::uint64_t{1} << 30U;
constexpr std::uint64_t kDiskBytes = 500 * kGiB;
constexpr std::size_t kFileBytes = 512;

// What a run reading `\\.\PhysicalDrive0` or `/dev/sda` covers: every byte of
// one disk, so every volume on it is inside the source.
[[nodiscard]] std::vector<StorageExtent> wholeDisk(std::uint64_t disk) {
	return {StorageExtent{.disk = disk, .offsetBytes = 0, .lengthBytes = kDiskBytes}};
}

// One volume, given where it starts on its disk and how long it is.
[[nodiscard]] std::vector<StorageExtent>
volumeOn(std::uint64_t disk, std::uint64_t startBytes, std::uint64_t lengthBytes) {
	return {StorageExtent{.disk = disk, .offsetBytes = startBytes, .lengthBytes = lengthBytes}};
}

TEST(DeviceIdentity, RefusesADestinationOnTheDiskBeingRead) {
	EXPECT_TRUE(overlaps(wholeDisk(kFirstDisk), volumeOn(kFirstDisk, kGiB, 100 * kGiB)));
}

TEST(DeviceIdentity, AllowsADestinationOnAnotherDisk) {
	EXPECT_FALSE(overlaps(wholeDisk(kFirstDisk), volumeOn(kSecondDisk, kGiB, 100 * kGiB)));
}

// The line this rule is drawn on: the loss mode is overwriting the clusters
// under recovery, and a sibling volume holds none of them.
TEST(DeviceIdentity, AllowsADestinationOnASiblingVolumeOfTheSameDisk) {
	EXPECT_FALSE(overlaps(
		volumeOn(kFirstDisk, kGiB, 100 * kGiB),
		volumeOn(kFirstDisk, 101 * kGiB, 100 * kGiB)));
}

TEST(DeviceIdentity, RefusesADestinationOnTheVolumeBeingRead) {
	EXPECT_TRUE(
		overlaps(volumeOn(kFirstDisk, kGiB, 100 * kGiB), volumeOn(kFirstDisk, kGiB, 100 * kGiB)));
}

// A spanned volume is several extents, and one of them landing on the source
// is as fatal as all of them would be.
TEST(DeviceIdentity, RefusesASpannedDestinationWithOneExtentOnTheSource) {
	const std::vector<StorageExtent> spanned{
		{.disk = kSecondDisk, .offsetBytes = kGiB, .lengthBytes = 100 * kGiB},
		{.disk = kFirstDisk, .offsetBytes = 200 * kGiB, .lengthBytes = 100 * kGiB}};
	EXPECT_TRUE(overlaps(wholeDisk(kFirstDisk), spanned));
}

// A network share resolves to no local storage at all, which overlaps nothing.
// ADR-0007 permits such a destination, so this is the answer that keeps it.
TEST(DeviceIdentity, AllowsADestinationOnNoLocalStorage) {
	EXPECT_FALSE(overlaps(wholeDisk(kFirstDisk), {}));
}

// Abutting is not overlapping — the off-by-one that would refuse the volume
// starting one byte after the source ends.
TEST(DeviceIdentity, AllowsADestinationStartingWhereTheSourceEnds) {
	EXPECT_FALSE(
		overlaps(volumeOn(kFirstDisk, kGiB, 100 * kGiB), volumeOn(kFirstDisk, 101 * kGiB, kGiB)));
}

TEST(DeviceIdentity, RefusesADestinationOverlappingTheSourcesLastByte) {
	EXPECT_TRUE(overlaps(
		volumeOn(kFirstDisk, kGiB, 100 * kGiB),
		volumeOn(kFirstDisk, (101 * kGiB) - 1, kGiB)));
}

// Resolution, over the only storage a CI runner reliably hands out: its own
// scratch directory. What that storage *is* varies per machine, and "no local
// disk at all" is a real answer rather than a failure — a runner whose scratch
// space is a tmpfs or a share gives exactly that, as WSL's `/tmp` does. So what
// is asserted is that the question is answered, and answered the same way twice
// for one filesystem. A device under it is proven on the workbench instead.
TEST(DeviceIdentity, AnswersTheStorageUnderARealDirectory) {
	const TempDir directory;
	const auto storage = storageUnder(directory.path());
	ASSERT_TRUE(storage.hasValue());
	const auto parent = storageUnder(std::filesystem::temp_directory_path());
	ASSERT_TRUE(parent.hasValue());
	EXPECT_EQ(storage.value(), parent.value());
}

// The fail-closed half: a path that names no device has no storage to read, so
// the question is refused rather than answered "nowhere", which would walk a
// run straight past the check.
TEST(DeviceIdentity, RefusesToResolveTheStorageOfARegularFile) {
	const TempFile file{std::vector<std::byte>(kFileBytes, std::byte{0})};
	EXPECT_FALSE(storageOf(file.path()).hasValue());
}

} // namespace
