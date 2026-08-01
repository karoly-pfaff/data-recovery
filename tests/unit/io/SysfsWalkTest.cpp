// SPDX-License-Identifier: GPL-3.0-or-later
// story-0609: the sysfs walk that traces a destination to the disks under it.
// Driven against a fixture tree rather than a running kernel, so the worklist,
// the descent through stacked devices and — above all — what refuses are
// checked on every build rather than only on a machine with LVM on it.
//
// The flat `<root>/<major>:<minor>` index the kernel builds out of symlinks is
// built here out of symlinks too. Where the platform will not make one without
// privilege the tests skip, which is the same line tests/unit/recovery draws
// for its alias case.
#include "core/io/SysfsWalk.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "revenant/core/io/DeviceIdentity.hpp"
#include "support/TempDir.hpp"

namespace {

using revenant::storageUnderSysfs;
using revenant::testing::TempDir;

constexpr std::uint64_t kSectorBytes = 512;

// Every node in this tree is described by two or more names that are all
// strings, so they are passed as fields rather than as a run of positional
// arguments nothing would stop a caller from transposing.
struct Disk {
	std::string name;
	std::string node;
};

struct Partition {
	std::string parent;
	std::string name;
	std::string node;
	std::uint64_t startSectors;
	std::uint64_t sizeSectors;
};

struct Stacked {
	std::string name;
	std::string node;
	std::string member;
	std::string memberNode;
};

// A sysfs block tree under construction: `devices/` holds the real nodes in
// their parent-child shape, and `block/` is the flat index of symlinks into it
// that `/sys/dev/block` is.
class SysfsTree {
public:
	explicit SysfsTree(const TempDir& root) : root_(root.path()) {
		std::filesystem::create_directories(index());
		std::filesystem::create_directories(root_ / "devices");
		// One probe up front, named exactly as a real node is. Two different
		// things can stop this fixture, and the probe has to feel both: a
		// platform that will not make a directory symlink without privilege,
		// and one where `major:minor` is not a legal filename at all. Probing
		// with a name of its own choosing answered the first question only —
		// an elevated Windows runner passed it and then failed on the colon.
		link("0:0", root_ / "devices");
		std::error_code ignored;
		std::filesystem::remove(index() / "0:0", ignored);
	}

	// A whole disk, named as sysfs names it.
	void disk(const Disk& entry) {
		const auto at = root_ / "devices" / entry.name;
		std::filesystem::create_directories(at / "slaves");
		write(at / "dev", entry.node);
		link(entry.node, at);
	}

	void partition(const Partition& entry) {
		const auto at = root_ / "devices" / entry.parent / entry.name;
		std::filesystem::create_directories(at);
		write(at / "dev", entry.node);
		write(at / "partition", "1");
		write(at / "start", std::to_string(entry.startSectors));
		write(at / "size", std::to_string(entry.sizeSectors));
		link(entry.node, at);
	}

	// A device built on one other, which is how sysfs records LVM, LUKS and md:
	// a `slaves` entry named after the member, holding the member's own `dev`.
	void stackedOn(const Stacked& entry) {
		const auto at = root_ / "devices" / entry.name;
		std::filesystem::create_directories(at / "slaves" / entry.member);
		write(at / "dev", entry.node);
		write(at / "slaves" / entry.member / "dev", entry.memberNode);
		link(entry.node, at);
	}

	[[nodiscard]] std::filesystem::path index() const {
		return root_ / "block";
	}

	// Whether the probe in the constructor succeeded — a platform that will not
	// make a directory symlink has no `/sys/dev/block` to imitate.
	[[nodiscard]] bool usable() const {
		return linked_;
	}

private:
	static void write(const std::filesystem::path& file, const std::string& text) {
		std::ofstream{file} << text << "\n";
	}

	// The flat index entry. Returns whether the platform allowed it — a caller
	// that cannot have symlinks has no sysfs to imitate.
	void link(const std::string& node, const std::filesystem::path& target) {
		std::error_code failed;
		std::filesystem::create_directory_symlink(target, index() / node, failed);
		linked_ = linked_ && !failed;
	}

	std::filesystem::path root_;
	bool linked_ = true;
};

// Every test needs a tree, and none of them can run where the flat index
// cannot be built — so the skip is asked once here instead of in each body.
class SysfsWalk : public ::testing::Test {
protected:
	void SetUp() override {
		if (!tree_.usable()) {
			GTEST_SKIP() << "this platform cannot build a `major:minor` symlink index";
		}
	}

	[[nodiscard]] SysfsTree& tree() {
		return tree_;
	}

	[[nodiscard]] const std::filesystem::path& root() const {
		return root_.path();
	}

private:
	TempDir root_;
	SysfsTree tree_{root_};
};

// Takes a directory's permissions away and says whether the platform and user
// actually honoured it: root reads a mode-000 directory regardless, and so
// does Windows.
[[nodiscard]] bool madeUnreadable(const std::filesystem::path& path) {
	std::error_code ignored;
	std::filesystem::permissions(path, std::filesystem::perms::none, ignored);
	std::error_code probe;
	static_cast<void>(std::filesystem::is_empty(path, probe));
	return static_cast<bool>(probe);
}

// A disk `8:0` with one partition `8:1` at sector 2048, 100 MiB long.
void plainDisk(SysfsTree& into) {
	into.disk({.name = "sda", .node = "8:0"});
	into.partition(
		{.parent = "sda",
		 .name = "sda1",
		 .node = "8:1",
		 .startSectors = 2048,
		 .sizeSectors = 204800});
}

TEST_F(SysfsWalk, ResolvesAPartitionToItsWindowOnTheDiskCarryingIt) {
	plainDisk(tree());
	const auto storage = storageUnderSysfs(tree().index(), "8:1");
	ASSERT_TRUE(storage.hasValue());
	ASSERT_EQ(storage.value().size(), 1U);
	EXPECT_EQ(storage.value().at(0).offsetBytes, 2048 * kSectorBytes);
	EXPECT_EQ(storage.value().at(0).lengthBytes, 204800 * kSectorBytes);
}

TEST_F(SysfsWalk, ResolvesAPlainDiskToEveryByteOfIt) {
	plainDisk(tree());
	const auto storage = storageUnderSysfs(tree().index(), "8:0");
	ASSERT_TRUE(storage.hasValue());
	ASSERT_EQ(storage.value().size(), 1U);
	EXPECT_EQ(storage.value().at(0).lengthBytes, revenant::kWholeDisk);
}

// The LVM case: a mapped device reports itself as a device of its own, and
// comparing that against the disk under it finds nothing in common. It has to
// resolve to what it is built from instead.
TEST_F(SysfsWalk, ResolvesAMappedDeviceToThePartitionItIsBuiltOn) {
	tree().disk({.name = "sda", .node = "8:0"});
	tree().partition(
		{.parent = "sda",
		 .name = "sda1",
		 .node = "8:1",
		 .startSectors = 2048,
		 .sizeSectors = 204800});
	tree().stackedOn({.name = "dm-0", .node = "253:0", .member = "sda1", .memberNode = "8:1"});
	const auto storage = storageUnderSysfs(tree().index(), "253:0");
	ASSERT_TRUE(storage.hasValue());
	ASSERT_EQ(storage.value().size(), 1U);
	EXPECT_EQ(storage.value().at(0).offsetBytes, 2048 * kSectorBytes);
}

// One level further than the mapped case: a *partition of* a RAID array is
// still on the disks the array is built from, and stopping at the array is how
// a destination on one of those disks gets allowed.
TEST_F(SysfsWalk, ResolvesAPartitionOfAStackedDeviceThroughToItsMembers) {
	tree().disk({.name = "sda", .node = "8:0"});
	tree().stackedOn({.name = "md0", .node = "9:0", .member = "sda", .memberNode = "8:0"});
	tree().partition(
		{.parent = "md0",
		 .name = "md0p1",
		 .node = "9:1",
		 .startSectors = 2048,
		 .sizeSectors = 204800});
	const auto storage = storageUnderSysfs(tree().index(), "9:1");
	ASSERT_TRUE(storage.hasValue());
	// Its own window on the array, and every byte of the disk under the array.
	ASSERT_EQ(storage.value().size(), 2U);
	EXPECT_EQ(storage.value().at(1).lengthBytes, revenant::kWholeDisk);
}

// The one case that needs no tree, so it is the one case that runs on every
// platform rather than only where a `major:minor` index can be built.
TEST(SysfsWalkMissingNode, RefusesANodeThatIsNotThere) {
	const TempDir root;
	EXPECT_FALSE(storageUnderSysfs(root.path() / "block", "8:99").hasValue());
}

// A device that is its own member would walk forever; the depth bound is what
// stops it, and stopping has to mean refusing rather than answering short.
TEST_F(SysfsWalk, RefusesADeviceStackedOnItself) {
	tree().stackedOn({.name = "dm-0", .node = "253:0", .member = "itself", .memberNode = "253:0"});
	EXPECT_FALSE(storageUnderSysfs(tree().index(), "253:0").hasValue());
}

// "Cannot tell whether this sits on other devices" is not "it does not". An
// unreadable `slaves` has to refuse, in both the places that ask: on the node
// itself, and on the device carrying a partition.
TEST_F(SysfsWalk, RefusesADeviceWhoseMembersCannotBeListed) {
	tree().disk({.name = "sda", .node = "8:0"});
	tree().stackedOn({.name = "dm-0", .node = "253:0", .member = "sda", .memberNode = "8:0"});
	const auto slaves = root() / "devices" / "dm-0" / "slaves";
	if (!madeUnreadable(slaves)) {
		GTEST_SKIP() << "this user can still read a directory with no permissions";
	}
	EXPECT_FALSE(storageUnderSysfs(tree().index(), "253:0").hasValue());
	std::error_code restoring;
	std::filesystem::permissions(slaves, std::filesystem::perms::owner_all, restoring);
}

// A member that cannot be named makes the union smaller than the truth, and a
// union smaller than the truth is what lets a destination through.
TEST_F(SysfsWalk, RefusesWhenAMemberCannotBeNamed) {
	tree().disk({.name = "sda", .node = "8:0"});
	tree().stackedOn({.name = "dm-0", .node = "253:0", .member = "sda", .memberNode = "8:0"});
	std::filesystem::remove(root() / "devices" / "dm-0" / "slaves" / "sda" / "dev");
	EXPECT_FALSE(storageUnderSysfs(tree().index(), "253:0").hasValue());
}

} // namespace
