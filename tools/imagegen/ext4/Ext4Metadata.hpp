// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "imagegen/ext4/Ext4Layout.hpp"
#include "imagegen/ext4/Ext4Records.hpp"

namespace revenant::imagegen::ext4 {

// The volume's own bookkeeping: the superblock and its single block group's
// descriptor. Split out of the image builder so a test can raise a volume of
// exactly this shape and put on it only the thing it is testing.
void putExt4Metadata(std::vector<std::byte>& image, const Ext4Layout& layout);

// One inode, at the place in the table its number gives it.
void putExt4Inode(
	std::vector<std::byte>& image,
	const Ext4Layout& layout,
	std::uint32_t number,
	const InodeSpec& spec);

// An empty volume of the fixture's shape: metadata written, nothing on it.
[[nodiscard]] std::vector<std::byte> emptyExt4Volume(const Ext4Layout& layout);

} // namespace revenant::imagegen::ext4
