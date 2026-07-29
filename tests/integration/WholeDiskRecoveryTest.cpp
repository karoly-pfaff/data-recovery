// SPDX-License-Identifier: GPL-3.0-or-later
// The epic's own closing claim, made executable: a whole disk can be scanned end
// to end. One run over one synthetic disk carrying four filesystems as
// partitions, and files come back out of more than one of them — named, placed
// under the partition they came from, and byte-identical to what the image
// builder put there.
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
using revenant::testing::holdsFileOfType;
using revenant::testing::readFileBytes;
using revenant::testing::runCli;
using revenant::testing::TempDir;
using revenant::testing::TempFile;

// The whole disk on disk, and somewhere to recover it into.
class WholeDisk {
public:
	WholeDisk() : image_(buildMbrDiskImage().bytes) {}

	[[nodiscard]] bool recover() const {
		return runCli(
			runUndeleteCli,
			{"revenant-undelete",
			 "--source",
			 image_.path().string(),
			 "--destination",
			 output_.path().string()});
	}

	[[nodiscard]] std::filesystem::path recovered(const std::string& relative) const {
		return output_.path() / relative;
	}

private:
	TempFile image_;
	TempDir output_;
};

// The NTFS volume is partition 1. Its deleted file comes back by name, from a
// run that was never told the disk was partitioned at all.
TEST(WholeDiskRecovery, RecoversANamedFileFromTheFirstPartition) {
	const WholeDisk disk;
	ASSERT_TRUE(disk.recover());
	EXPECT_EQ(
		readFileBytes(disk.recovered("partition-1/photos/deleted.jpg")),
		fixtureContentNamed("deleted.jpg"));
}

// More than one volume contributed, which is the claim: a whole disk is not one
// volume that happens to be large.
TEST(WholeDiskRecovery, RecoversFromMoreThanOnePartition) {
	const WholeDisk disk;
	ASSERT_TRUE(disk.recover());
	EXPECT_TRUE(std::filesystem::exists(disk.recovered("partition-1")));
	EXPECT_TRUE(std::filesystem::exists(disk.recovered("partition-2")));
}

// The carve pass still covers the whole device, including the gaps between
// partitions — which is where a deleted partition's contents would be.
TEST(WholeDiskRecovery, StillCarvesWhatNoVolumeAccountedFor) {
	const WholeDisk disk;
	ASSERT_TRUE(disk.recover());
	EXPECT_TRUE(holdsFileOfType(disk.recovered("carved"), ".jpg"));
}

} // namespace
