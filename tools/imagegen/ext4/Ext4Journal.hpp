// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace revenant::imagegen::ext4 {

// One filesystem block as the journal still remembers it, from a transaction
// before whatever changed it since.
struct RememberedBlock {
	std::uint32_t fileSystemBlock;
	std::vector<std::byte> content;
};

// The journal file's blocks, in its own numbering: its superblock, one
// descriptor block announcing `remembered`, and the copy that descriptor
// announces. Big-endian throughout — jbd2 kept the byte order it was written
// with, whichever way the filesystem around it stores its fields.
[[nodiscard]] std::vector<std::vector<std::byte>>
journalBlocks(const RememberedBlock& remembered, std::size_t blockSizeBytes);

} // namespace revenant::imagegen::ext4
