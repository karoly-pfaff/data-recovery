// SPDX-License-Identifier: GPL-3.0-or-later
// story-0609: which device a destination's filesystem is actually mounted
// from. Text in, answer out, so the part that is easy to get wrong is tested on
// every platform rather than only where /proc exists.
#include "core/io/MountTable.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/io/DeviceNumber.hpp"

namespace {

using revenant::mountSourceFor;

// One number per mountinfo `major:minor`. Built through `deviceKey` rather
// than restated here, because restating it is what hid the defect this guards:
// a raw `dev_t` packs the same two halves differently, matches no line, and
// silently drops the selection back to the depth rule it was added to replace.
[[nodiscard]] constexpr std::uint64_t dev(std::uint64_t major, std::uint64_t minor) {
	return revenant::deviceKey(major, minor);
}

// A mount table with the shapes that broke earlier cuts of this story: a btrfs
// mount, whose `0:52` belongs to no block device at all; a device-mapper mount,
// whose `253:0` is a device of its own rather than the disk underneath it; a
// network mount, which must stay allowed; and an overlay, whose source is not a
// device at all.
constexpr std::string_view kMountInfo =
	"23 28 0:21 / /proc rw,nosuid,nodev,noexec,relatime shared:12 - proc proc rw\n"
	"82 67 8:48 / / rw,relatime - ext4 /dev/sdd rw,errors=remount-ro\n"
	"90 82 0:52 / /home rw,relatime - btrfs /dev/sda3 rw,subvol=/@home\n"
	"95 82 253:0 / /mnt/data rw,relatime - xfs /dev/mapper/vg-data rw\n"
	"99 82 0:60 / /mnt/share rw,relatime - nfs4 server:/export rw\n"
	"101 82 8:17 / /mnt/with\\040space rw,relatime - ext4 /dev/sdb1 rw\n"
	"104 82 0:71 / /var/lib/containers rw,relatime - overlay overlay rw,upperdir=/x\n";

[[nodiscard]] std::string
sourceIn(std::string_view table, std::string_view path, std::uint64_t fsDevice) {
	const auto mount = mountSourceFor(table, path, fsDevice);
	return mount.has_value() ? mount->source : std::string{"<none>"};
}

[[nodiscard]] std::string sourceFor(std::string_view path, std::uint64_t fsDevice) {
	return sourceIn(kMountInfo, path, fsDevice);
}

[[nodiscard]] std::string typeFor(std::string_view path, std::uint64_t fsDevice) {
	const auto mount = mountSourceFor(kMountInfo, path, fsDevice);
	return mount.has_value() ? mount->type : std::string{"<none>"};
}

TEST(MountTable, TakesTheLongestMountPointCoveringThePath) {
	EXPECT_EQ(sourceFor("/home/user/out", dev(0, 52)), "/dev/sda3");
}

TEST(MountTable, FallsBackToTheRootMountWhenNothingDeeperCovers) {
	EXPECT_EQ(sourceFor("/var/tmp/out", dev(8, 48)), "/dev/sdd");
}

// The whole reason this file exists: a btrfs filesystem's `major:minor` is an
// anonymous number no block device owns, so reading it as the destination's
// storage answers "no local disk" for a directory sitting on one.
TEST(MountTable, AnswersTheBackingDeviceRatherThanTheFilesystemsOwnNumber) {
	EXPECT_EQ(sourceFor("/home", dev(0, 52)), "/dev/sda3");
}

// A mapped device names itself, not the disk it is built from; resolving that
// is sysfs's job, and this only has to hand over the right name.
TEST(MountTable, AnswersAMappedDeviceByItsOwnName) {
	EXPECT_EQ(sourceFor("/mnt/data/out", dev(253, 0)), "/dev/mapper/vg-data");
}

// ADR-0007 permits a network destination; it is recognised by its type, so the
// type has to survive the parse.
TEST(MountTable, AnswersANetworkMountsTypeAndSource) {
	EXPECT_EQ(typeFor("/mnt/share/out", dev(0, 60)), "nfs4");
	EXPECT_EQ(sourceFor("/mnt/share/out", dev(0, 60)), "server:/export");
}

// An overlay's source is a bare word, not a device. Only the type tells it
// apart from a mount this build simply failed to trace, and the two must not
// end up with the same answer.
TEST(MountTable, AnswersAnOverlayByItsTypeRatherThanADevice) {
	EXPECT_EQ(typeFor("/var/lib/containers/out", dev(0, 71)), "overlay");
	EXPECT_EQ(sourceFor("/var/lib/containers/out", dev(0, 71)), "overlay");
}

TEST(MountTable, UnescapesAMountPointHoldingASpace) {
	EXPECT_EQ(sourceFor("/mnt/with space/out", dev(8, 17)), "/dev/sdb1");
}

// A near-miss must not be read as a prefix: `/mnt/data` does not cover
// `/mnt/database`, and matching by characters rather than by path elements
// would say it does.
TEST(MountTable, DoesNotTreatASiblingNameAsACoveringMount) {
	EXPECT_EQ(sourceFor("/mnt/database/out", dev(8, 48)), "/dev/sdd");
}

// The deepest covering mount is not always the live one: a mount point can be
// shadowed by another mounted over an ancestor of it, and the shadowed entry
// stays in the table covering a path it no longer holds a byte of. The
// filesystem's own number is what tells them apart.
TEST(MountTable, PrefersTheMountTheFilesystemNumberNames) {
	constexpr std::string_view kShadowed = "1 0 8:1 / / rw - ext4 /dev/sda1 rw\n"
										   "2 1 8:17 / /mnt/x rw - ext4 /dev/sdb1 rw\n"
										   "3 1 8:33 / /mnt rw - ext4 /dev/sdc1 rw\n";
	EXPECT_EQ(sourceIn(kShadowed, "/mnt/x/out", dev(8, 33)), "/dev/sdc1");
}

// The allowlist is the whole of what makes "no local storage" a real answer
// rather than a failure to trace. A network share and a tmpfs hold none; an
// overlay's upper layer can be anywhere, including on the disk being recovered,
// so it must not be waved through with them.
TEST(MountTable, KnowsWhichFilesystemsHoldNoLocalStorage) {
	EXPECT_TRUE(revenant::holdsNoLocalStorage("nfs4"));
	EXPECT_TRUE(revenant::holdsNoLocalStorage("cifs"));
	EXPECT_TRUE(revenant::holdsNoLocalStorage("tmpfs"));
	EXPECT_FALSE(revenant::holdsNoLocalStorage("overlay"));
	EXPECT_FALSE(revenant::holdsNoLocalStorage("ext4"));
	EXPECT_FALSE(revenant::holdsNoLocalStorage("btrfs"));
	EXPECT_FALSE(revenant::holdsNoLocalStorage("zfs"));
	EXPECT_FALSE(revenant::holdsNoLocalStorage(""));
}

TEST(MountTable, AnswersNothingWhenNoMountCoversThePath) {
	EXPECT_FALSE(mountSourceFor("", "/home/user", dev(8, 1)).has_value());
}

TEST(MountTable, IgnoresALineWithNoSeparatorField) {
	EXPECT_FALSE(mountSourceFor("82 67 8:48 / / rw,relatime ext4 /dev/sdd rw\n", "/", dev(8, 48))
					 .has_value());
}

// A line that ends at the separator has no type and no source to read. The
// guard has to notice before it walks off the end of the field list.
TEST(MountTable, IgnoresALineThatEndsAtTheSeparator) {
	EXPECT_FALSE(mountSourceFor("1 0 8:1 / / rw -\n", "/", dev(8, 1)).has_value());
	EXPECT_FALSE(mountSourceFor("1 0 8:1 / / rw - ext4\n", "/", dev(8, 1)).has_value());
}

TEST(MountTable, IgnoresATruncatedLine) {
	EXPECT_FALSE(mountSourceFor("1 0 8:1 /\n", "/", dev(8, 1)).has_value());
}

// A partial escape is not an escape. Reading one as though it were would eat
// the two characters after it and silently name a different mount point.
TEST(MountTable, LeavesAPartialOctalEscapeAlone) {
	constexpr std::string_view kOdd = "1 0 8:1 / /mnt/a\\1bc rw - ext4 /dev/sda1 rw\n";
	EXPECT_TRUE(mountSourceFor(kOdd, "/mnt/a\\1bc/out", dev(8, 1)).has_value());
	EXPECT_FALSE(mountSourceFor(kOdd, "/mnt/a\x01/out", dev(8, 1)).has_value());
}

} // namespace
