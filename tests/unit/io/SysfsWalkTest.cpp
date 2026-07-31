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
#include <fstream>
#include <string>
#include <system_error>

#include "revenant/core/Error.hpp"
#include "support/TempDir.hpp"

namespace {

using revenant::storageUnderSysfs;
using revenant::testing::TempDir;

constexpr std::uint64_t kSectorBytes = 512;

// A sysfs block tree under construction: `devices/` holds the real nodes in
// their parent-child shape, and `block/` is the flat index of symlinks into it
// that `/sys/dev/block` is.
class SysfsTree {
public:
	explicit SysfsTree(const TempDir& root) : root_(root.path()) {
		std::filesystem::create_directories(index());
		std::filesystem::create_directories(root_ / "devices");
	}

	// A whole disk, named as sysfs names it.
	void disk(const std::string& name, const std::string& node) {
		const auto at = root_ / "devices" / name;
		std::filesystem::create_directories(at / "slaves");
		write(at / "dev", node);
		link(node, at);
	}

	// A partition of `parent`, at `startSectors` for `sizeSectors`.
	void partition(
		const std::string& parent,
		const std::string& name,
		const std::string& node,
		std::uint64_t startSectors,
		std::uint64_t sizeSectors) {
		const auto at = root_ / "devices" / parent / name;
		std::filesystem::create_directories(at);
		write(at / "dev", node);
		write(at / "partition", "1");
		write(at / "start", std::to_string(startSectors));
		write(at / "size", std::to_string(sizeSectors));
		link(node, at);
	}

	// A device built on one other, which is how sysfs records LVM, LUKS and md:
	// a `slaves` entry named after the member, holding the member's own `dev`.
	void stackedOn(
		const std::string& name,
		const std::string& node,
		const std::string& member,
		const std::string& memberNode) {
		const auto at = root_ / "devices" / name;
		std::filesystem::create_directories(at / "slaves" / member);
		write(at / "dev", node);
		write(at / "slaves" / member / "dev", memberNode);
		link(node, at);
	}

	[[nodiscard]] std::filesystem::path index() const {
		return root_ / "block";
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

public:
	[[nodiscard]] bool usable() const {
		return linked_;
	}
};

// A disk `8:0` with one partition `8:1` at sector 2048, 100 MiB long.
[[nodiscard]] SysfsTree plainDisk(const TempDir& root) {
	SysfsTree tree{root};
	tree.disk("sda", "8:0");
	tree.partition("sda", "sda1", "8:1", 2048, 204800);
	return tree;
}

TEST(SysfsWalk, ResolvesAPartitionToItsWindowOnTheDiskCarryingIt) {
	const TempDir root;
	const auto tree = plainDisk(root);
	if (!tree.usable()) {
		GTEST_SKIP() << "this platform will not create directory symlinks unprivileged";
	}
	const auto storage = storageUnderSysfs(tree.index(), "8:1");
	ASSERT_TRUE(storage.hasValue());
	ASSERT_EQ(storage.value().size(), 1U);
	EXPECT_EQ(storage.value().at(0).offsetBytes, 2048 * kSectorBytes);
	EXPECT_EQ(storage.value().at(0).lengthBytes, 204800 * kSectorBytes);
}

TEST(SysfsWalk, ResolvesAPlainDiskToEveryByteOfIt) {
	const TempDir root;
	const auto tree = plainDisk(root);
	if (!tree.usable()) {
		GTEST_SKIP() << "this platform will not create directory symlinks unprivileged";
	}
	const auto storage = storageUnderSysfs(tree.index(), "8:0");
	ASSERT_TRUE(storage.hasValue());
	ASSERT_EQ(storage.value().size(), 1U);
	EXPECT_EQ(storage.value().at(0).lengthBytes, revenant::kWholeDisk);
}

// The LVM case: a mapped device reports itself as a device of its own, and
// comparing that against the disk under it finds nothing in common. It has to
// resolve to what it is built from instead.
TEST(SysfsWalk, ResolvesAMappedDeviceToThePartitionItIsBuiltOn) {
	const TempDir root;
	SysfsTree tree{root};
	tree.disk("sda", "8:0");
	tree.partition("sda", "sda1", "8:1", 2048, 204800);
	tree.stackedOn("dm-0", "253:0", "sda1", "8:1");
	if (!tree.usable()) {
		GTEST_SKIP() << "this platform will not create directory symlinks unprivileged";
	}
	const auto storage = storageUnderSysfs(tree.index(), "253:0");
	ASSERT_TRUE(storage.hasValue());
	ASSERT_EQ(storage.value().size(), 1U);
	EXPECT_EQ(storage.value().at(0).offsetBytes, 2048 * kSectorBytes);
}

// One level further than the mapped case: a *partition of* a RAID array is
// still on the disks the array is built from, and stopping at the array is how
// a destination on one of those disks gets allowed.
TEST(SysfsWalk, ResolvesAPartitionOfAStackedDeviceThroughToItsMembers) {
	const TempDir root;
	SysfsTree tree{root};
	tree.disk("sda", "8:0");
	tree.stackedOn("md0", "9:0", "sda", "8:0");
	tree.partition("md0", "md0p1", "9:1", 2048, 204800);
	if (!tree.usable()) {
		GTEST_SKIP() << "this platform will not create directory symlinks unprivileged";
	}
	const auto storage = storageUnderSysfs(tree.index(), "9:1");
	ASSERT_TRUE(storage.hasValue());
	// Its own window on the array, and every byte of the disk under the array.
	ASSERT_EQ(storage.value().size(), 2U);
	EXPECT_EQ(storage.value().at(1).lengthBytes, revenant::kWholeDisk);
}

TEST(SysfsWalk, RefusesANodeThatIsNotThere) {
	const TempDir root;
	const SysfsTree tree{root};
	EXPECT_FALSE(storageUnderSysfs(tree.index(), "8:99").hasValue());
}

// A device that is its own member would walk forever; the depth bound is what
// stops it, and stopping has to mean refusing rather than answering short.
TEST(SysfsWalk, RefusesADeviceStackedOnItself) {
	const TempDir root;
	SysfsTree tree{root};
	tree.stackedOn("dm-0", "253:0", "itself", "253:0");
	if (!tree.usable()) {
		GTEST_SKIP() << "this platform will not create directory symlinks unprivileged";
	}
	EXPECT_FALSE(storageUnderSysfs(tree.index(), "253:0").hasValue());
}

// A member that cannot be named makes the union smaller than the truth, and a
// union smaller than the truth is what lets a destination through.
TEST(SysfsWalk, RefusesWhenAMemberCannotBeNamed) {
	const TempDir root;
	SysfsTree tree{root};
	tree.disk("sda", "8:0");
	tree.stackedOn("dm-0", "253:0", "sda", "8:0");
	if (!tree.usable()) {
		GTEST_SKIP() << "this platform will not create directory symlinks unprivileged";
	}
	std::filesystem::remove(root.path() / "devices" / "dm-0" / "slaves" / "sda" / "dev");
	EXPECT_FALSE(storageUnderSysfs(tree.index(), "253:0").hasValue());
}

} // namespace
