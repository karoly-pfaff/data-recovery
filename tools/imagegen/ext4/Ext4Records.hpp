// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace revenant::imagegen::ext4 {

// One run of a file's blocks, as the fixture states it.
struct ExtentSpec {
	std::uint32_t firstFileBlock;
	std::uint32_t blockCount;
	std::uint32_t firstDeviceBlock;
};

// The 60 bytes an inode's `i_block` holds: an extent-tree header and its leaves.
// An empty run list yields sixty zero bytes — which is what a kernel that wipes
// an inode's tree on deletion leaves behind, and the case the journal hint
// exists for.
[[nodiscard]] std::vector<std::byte> extentTree(const std::vector<ExtentSpec>& runs);

// The runs' blocks one by one, in file order — the flat list a content writer
// spreads a file across.
[[nodiscard]] std::vector<std::uint32_t> blocksOf(const std::vector<ExtentSpec>& runs);

// One inode as the fixture states it. `links` is what tells a live inode from a
// freed one; `deletionTime` doubles as the orphan list's next pointer, which is
// how ext4 chains that list.
struct InodeSpec {
	std::uint16_t mode;
	std::uint16_t links;
	std::uint64_t sizeInBytes;
	std::uint32_t deletionTime;
	std::vector<ExtentSpec> runs;
};

[[nodiscard]] std::vector<std::byte> inodeRecord(const InodeSpec& spec, std::size_t inodeBytes);

// One linear directory entry, padded out to `recordBytes`. A deleted entry is
// written exactly the same way — it is the *previous* entry's record length that
// hides it, not anything about the entry itself.
struct DirEntrySpec {
	std::uint32_t inode;
	std::string_view name;
	std::uint8_t fileType;
	std::uint16_t recordBytes;
};

[[nodiscard]] std::vector<std::byte> dirEntry(const DirEntrySpec& spec);

} // namespace revenant::imagegen::ext4
