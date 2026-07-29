// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/ext4/OrphanList.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "fs/ext4/InodeTable.hpp"
#include "revenant/fs/ext4/Inode.hpp"

namespace revenant::fs::ext4 {

namespace {

// Where the chain goes next, or 0 when it ends here. An inode that will not
// read, or that never held anything, ends the list — the number that would
// follow it came out of the same bytes.
[[nodiscard]] std::uint32_t nextOrphan(const Ext4InodeTable& inodes, std::uint32_t number) {
	const auto inode = inodes.read(number);
	if (!inode.hasValue() || inode.value().isUnused) {
		return 0;
	}
	return inode.value().deletionTime;
}

[[nodiscard]] bool alreadySeen(const std::vector<std::uint32_t>& found, std::uint32_t number) {
	return std::ranges::find(found, number) != found.end();
}

} // namespace

std::vector<std::uint32_t> orphanInodes(const Ext4InodeTable& inodes, std::uint32_t head) {
	std::vector<std::uint32_t> found;
	std::uint32_t number = head;
	while (number != 0 && found.size() < kMaxOrphans && !alreadySeen(found, number)) {
		if (!inodes.offsetOf(number).hasValue()) {
			return found;
		}
		found.push_back(number);
		number = nextOrphan(inodes, number);
	}
	return found;
}

} // namespace revenant::fs::ext4
