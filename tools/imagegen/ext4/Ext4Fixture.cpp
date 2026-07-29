// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ext4/Ext4Fixture.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "imagegen/FixtureBytes.hpp"
#include "imagegen/ext4/Ext4Layout.hpp"
#include "imagegen/ext4/Ext4Records.hpp"

namespace revenant::imagegen::ext4 {

namespace {

// `keep.txt` is deliberately in two runs a long way apart, so reading it back
// exactly proves its extent tree was followed rather than its start assumed.
[[nodiscard]] std::vector<ExtentSpec> keepRuns() {
	return {
		ExtentSpec{.firstFileBlock = 0, .blockCount = 2, .firstDeviceBlock = kKeepFirstBlock},
		ExtentSpec{.firstFileBlock = 2, .blockCount = 2, .firstDeviceBlock = kKeepSecondBlock}};
}

[[nodiscard]] std::vector<ExtentSpec> oneRun(std::uint32_t first, std::uint32_t count) {
	return {ExtentSpec{.firstFileBlock = 0, .blockCount = count, .firstDeviceBlock = first}};
}

} // namespace

std::vector<Ext4File> rootFiles() {
	std::vector<Ext4File> files;
	files.push_back(
		Ext4File{
			.name = "keep.txt",
			.inode = kKeepInode,
			.runs = keepRuns(),
			.live = true,
			.treeWiped = false,
			.content = fixtureContent(3000, std::byte{1})});
	files.push_back(
		Ext4File{
			.name = "gone.txt",
			.inode = kGoneInode,
			.runs = oneRun(kGoneBlock, 2),
			.live = false,
			.treeWiped = false,
			.content = fixtureContent(1500, std::byte{2})});
	files.push_back(
		Ext4File{
			.name = "wiped.txt",
			.inode = kWipedInode,
			.runs = oneRun(kWipedBlock, 2),
			.live = false,
			.treeWiped = true,
			.content = fixtureContent(1600, std::byte{3})});
	files.push_back(
		Ext4File{
			.name = "later.bin",
			.inode = kLaterInode,
			.runs = oneRun(kLaterBlock, 1),
			.live = true,
			.treeWiped = false,
			.content = fixtureContent(500, std::byte{5})});
	return files;
}

std::vector<Ext4File> photosFiles() {
	std::vector<Ext4File> files;
	files.push_back(
		Ext4File{
			.name = "inner.bin",
			.inode = kInnerInode,
			.runs = oneRun(kInnerBlock, 1),
			.live = true,
			.treeWiped = false,
			.content = fixtureContent(900, std::byte{4})});
	return files;
}

Ext4File orphanFile() {
	return Ext4File{
		.name = {},
		.inode = kOrphanInode,
		.runs = oneRun(kOrphanBlock, 1),
		.live = false,
		.treeWiped = false,
		.content = fixtureContent(700, std::byte{6})};
}

} // namespace revenant::imagegen::ext4
