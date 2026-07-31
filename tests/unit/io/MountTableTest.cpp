// SPDX-License-Identifier: GPL-3.0-or-later
// story-0609: which device a destination's filesystem is actually mounted
// from. Text in, answer out, so the part that is easy to get wrong is tested on
// every platform rather than only where /proc exists.
#include "core/io/MountTable.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string_view>

namespace {

using revenant::mountSourceFor;

// A mount table with the three shapes that broke the first cut of this story:
// a btrfs mount, whose `0:52` belongs to no block device at all; a device-mapper
// mount, whose `253:0` is a device of its own rather than the disk underneath
// it; and a network mount, which must stay allowed.
constexpr std::string_view kMountInfo =
	"23 28 0:21 / /proc rw,nosuid,nodev,noexec,relatime shared:12 - proc proc rw\n"
	"82 67 8:48 / / rw,relatime - ext4 /dev/sdd rw,errors=remount-ro\n"
	"90 82 0:52 / /home rw,relatime - btrfs /dev/sda3 rw,subvol=/@home\n"
	"95 82 253:0 / /mnt/data rw,relatime - xfs /dev/mapper/vg-data rw\n"
	"99 82 0:60 / /mnt/share rw,relatime - nfs4 server:/export rw\n"
	"101 82 8:17 / /mnt/with\\040space rw,relatime - ext4 /dev/sdb1 rw\n";

TEST(MountTable, TakesTheLongestMountPointCoveringThePath) {
	EXPECT_EQ(mountSourceFor(kMountInfo, "/home/user/out"), "/dev/sda3");
}

TEST(MountTable, FallsBackToTheRootMountWhenNothingDeeperCovers) {
	EXPECT_EQ(mountSourceFor(kMountInfo, "/var/tmp/out"), "/dev/sdd");
}

// The whole reason this file exists: a btrfs filesystem's `major:minor` is an
// anonymous number no block device owns, so reading it as the destination's
// storage answers "no local disk" for a directory sitting on one.
TEST(MountTable, AnswersTheBackingDeviceRatherThanTheFilesystemsOwnNumber) {
	EXPECT_NE(mountSourceFor(kMountInfo, "/home"), "0:52");
	EXPECT_EQ(mountSourceFor(kMountInfo, "/home"), "/dev/sda3");
}

// A mapped device names itself, not the disk it is built from; resolving that
// is sysfs's job, and this only has to hand over the right name.
TEST(MountTable, AnswersAMappedDeviceByItsOwnName) {
	EXPECT_EQ(mountSourceFor(kMountInfo, "/mnt/data/out"), "/dev/mapper/vg-data");
}

// ADR-0007 permits a network destination; it is recognised by its source not
// being a block device, so the source has to survive intact to be recognised.
TEST(MountTable, AnswersANetworkMountsSourceUnchanged) {
	EXPECT_EQ(mountSourceFor(kMountInfo, "/mnt/share/out"), "server:/export");
}

TEST(MountTable, UnescapesAMountPointHoldingASpace) {
	EXPECT_EQ(mountSourceFor(kMountInfo, "/mnt/with space/out"), "/dev/sdb1");
}

// A near-miss must not be read as a prefix: `/mnt/data` does not cover
// `/mnt/database`, and matching by characters rather than by path elements
// would say it does.
TEST(MountTable, DoesNotTreatASiblingNameAsACoveringMount) {
	EXPECT_EQ(mountSourceFor(kMountInfo, "/mnt/database/out"), "/dev/sdd");
}

TEST(MountTable, AnswersNothingWhenNoMountCoversThePath) {
	EXPECT_FALSE(mountSourceFor("", "/home/user").has_value());
}

TEST(MountTable, IgnoresALineWithNoSeparatorField) {
	EXPECT_FALSE(mountSourceFor("82 67 8:48 / / rw,relatime ext4 /dev/sdd rw\n", "/").has_value());
}

} // namespace
