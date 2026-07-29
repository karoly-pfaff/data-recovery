// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/ext4/DirectoryBytes.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fs/ext4/BlockReader.hpp"
#include "fs/ext4/EntryFromInode.hpp"
#include "fs/ext4/ExtentWalk.hpp"
#include "fs/ext4/InodeTable.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/fs/ext4/Inode.hpp"

namespace revenant::fs::ext4 {

Result<std::vector<std::byte>> readDirectoryBytes(const EntrySource& source, std::uint32_t number) {
	return source.inodes->read(number).andThen([&source](const Ext4Inode& inode) {
		return inodeExtents(*source.blocks, inode)
			.andThen([&source](const std::vector<Extent>& extents) {
				return readExtents(*source.blocks, extents, kMaxDirectoryBytes);
			});
	});
}

} // namespace revenant::fs::ext4
