// SPDX-License-Identifier: GPL-3.0-or-later
// story-0405: a whole disk, and a run aimed at one of its partitions. What is
// under test is that the window actually confines the run — the NTFS volume's
// files come back when partition 1 is named, and do not when partition 2 is.
// Nothing below the CLI knows partitions exist; a PartitionView is a BlockDevice
// like any other, which is the whole point of the seam.
#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "cli/UndeleteCli.hpp"
#include "imagegen/disk/DiskImageBuilder.hpp"
#include "support/CliFixture.hpp"
#include "support/FixtureContent.hpp"
#include "support/TempDir.hpp"
#include "support/TempFile.hpp"

namespace {

using revenant::cli::runUndeleteCli;
using revenant::imagegen::disk::buildMbrDiskImage;
using revenant::testing::fixtureContentNamed;
using revenant::testing::readFileBytes;
using revenant::testing::runCli;
using revenant::testing::TempDir;
using revenant::testing::TempFile;

// The NTFS fixture is partition 1 of the synthetic disk, and the file it holds
// under `photos/` is the one every NTFS test asserts on.
constexpr const char* kNtfsPartition = "1";
constexpr const char* kFat32Partition = "2";

// A whole disk on disk, plus somewhere to recover into.
class PartitionedDisk {
public:
	PartitionedDisk() : image_(buildMbrDiskImage().bytes) {}

	[[nodiscard]] bool undeleteFrom(const std::string& partition) const {
		return runCli(
			runUndeleteCli,
			{"revenant-undelete",
			 "--source",
			 image_.path().string(),
			 "--destination",
			 output_.path().string(),
			 "--partition",
			 partition});
	}

	[[nodiscard]] std::filesystem::path recovered(const std::string& relative) const {
		return output_.path() / relative;
	}

private:
	TempFile image_;
	TempDir output_;
};

TEST(PartitionSelection, RecoversTheNamedPartitionsFiles) {
	const PartitionedDisk disk;
	ASSERT_TRUE(disk.undeleteFrom(kNtfsPartition));
	EXPECT_EQ(
		readFileBytes(disk.recovered("photos/deleted.jpg")),
		fixtureContentNamed("deleted.jpg"));
}

// The same disk, a different window: the NTFS volume is outside it, so its
// files are not there to be found.
TEST(PartitionSelection, DoesNotReachIntoAnotherPartition) {
	const PartitionedDisk disk;
	ASSERT_TRUE(disk.undeleteFrom(kFat32Partition));
	EXPECT_FALSE(std::filesystem::exists(disk.recovered("photos/deleted.jpg")));
}

// A partition the table does not describe is a refusal, not a whole-disk run:
// recovering the wrong range is worse than recovering nothing.
TEST(PartitionSelection, RefusesAPartitionTheTableDoesNotHave) {
	const PartitionedDisk disk;
	EXPECT_FALSE(disk.undeleteFrom("9"));
}

} // namespace
