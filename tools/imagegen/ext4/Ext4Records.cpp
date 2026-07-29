// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/ext4/Ext4Records.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "imagegen/ByteWriter.hpp"

namespace revenant::imagegen::ext4 {

namespace {

constexpr std::size_t kBlockMapBytes = 60;
constexpr std::size_t kExtentHeaderBytes = 12;
constexpr std::size_t kExtentEntryBytes = 12;
constexpr std::uint16_t kExtentMagic = 0xF30A;
constexpr std::uint16_t kMaxInlineExtents = 4;

constexpr std::uint32_t kExtentsFlag = 0x0008'0000;
constexpr std::size_t kBlockMapOffset = 0x28;

// 2020-08-01 12:00:00 UTC, on every inode the fixture writes, so a test can
// assert a timestamp without restating a date.
constexpr std::uint32_t kFixtureSeconds = 1'596'283'200;

void putLeaf(std::vector<std::byte>& tree, std::size_t at, const ExtentSpec& run) {
	putLe<std::uint32_t>(tree, at + 0x00, run.firstFileBlock);
	putLe<std::uint16_t>(tree, at + 0x04, static_cast<std::uint16_t>(run.blockCount));
	putLe<std::uint32_t>(tree, at + 0x08, run.firstDeviceBlock);
}

void putTreeHeader(std::vector<std::byte>& tree, std::uint16_t entries) {
	putLe<std::uint16_t>(tree, 0x00, kExtentMagic);
	putLe<std::uint16_t>(tree, 0x02, entries);
	putLe<std::uint16_t>(tree, 0x04, kMaxInlineExtents);
}

void putLeaves(std::vector<std::byte>& tree, const std::vector<ExtentSpec>& runs) {
	for (std::size_t at = 0; at < runs.size(); ++at) {
		putLeaf(tree, kExtentHeaderBytes + (at * kExtentEntryBytes), runs.at(at));
	}
}

void putTimes(std::vector<std::byte>& record) {
	putLe<std::uint32_t>(record, 0x08, kFixtureSeconds); // i_atime
	putLe<std::uint32_t>(record, 0x0C, kFixtureSeconds); // i_ctime
	putLe<std::uint32_t>(record, 0x10, kFixtureSeconds); // i_mtime
	putLe<std::uint16_t>(record, 0x80, 32);              // i_extra_isize
	putLe<std::uint32_t>(record, 0x90, kFixtureSeconds); // i_crtime
}

} // namespace

std::vector<std::byte> extentTree(const std::vector<ExtentSpec>& runs) {
	std::vector<std::byte> tree(kBlockMapBytes, std::byte{0});
	if (runs.empty()) {
		return tree;
	}
	putTreeHeader(tree, static_cast<std::uint16_t>(runs.size()));
	putLeaves(tree, runs);
	return tree;
}

std::vector<std::uint32_t> blocksOf(const std::vector<ExtentSpec>& runs) {
	std::vector<std::uint32_t> blocks;
	for (const ExtentSpec& run : runs) {
		for (std::uint32_t at = 0; at < run.blockCount; ++at) {
			blocks.push_back(run.firstDeviceBlock + at);
		}
	}
	return blocks;
}

std::vector<std::byte> inodeRecord(const InodeSpec& spec, std::size_t inodeBytes) {
	std::vector<std::byte> record(inodeBytes, std::byte{0});
	putLe<std::uint16_t>(record, 0x00, spec.mode);
	putLe<std::uint32_t>(record, 0x04, static_cast<std::uint32_t>(spec.sizeInBytes));
	putLe<std::uint32_t>(record, 0x14, spec.deletionTime);
	putLe<std::uint16_t>(record, 0x1A, spec.links);
	putLe<std::uint32_t>(record, 0x20, kExtentsFlag);
	putTimes(record);
	putBytes(record, kBlockMapOffset, extentTree(spec.runs));
	return record;
}

std::vector<std::byte> dirEntry(const DirEntrySpec& spec) {
	std::vector<std::byte> record(spec.recordBytes, std::byte{0});
	putLe<std::uint32_t>(record, 0x00, spec.inode);
	putLe<std::uint16_t>(record, 0x04, spec.recordBytes);
	putLe<std::uint8_t>(record, 0x06, static_cast<std::uint8_t>(spec.name.size()));
	putLe<std::uint8_t>(record, 0x07, spec.fileType);
	putBytes(record, 0x08, std::as_bytes(std::span{spec.name}));
	return record;
}

} // namespace revenant::imagegen::ext4
