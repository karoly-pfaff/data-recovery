// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "imagegen/ext4/Ext4Records.hpp"

namespace revenant::imagegen::ext4 {

// One file on the synthetic volume.
struct Ext4File {
	std::string_view name;
	std::uint32_t inode;
	std::vector<ExtentSpec> runs;
	bool live;
	// The deletion zeroed this inode's extent tree, as many kernels do. Its
	// blocks are still written and its name is still findable, but nothing on
	// disk says where the bytes are — which is the case the journal hint exists
	// for, and the reason ext4 undelete is harder than exFAT's.
	bool treeWiped;
	std::vector<std::byte> content;
};

// The root's files: one live and fragmented, one deleted with its extent tree
// intact, one deleted whose tree the deletion wiped, and one live file whose
// inode a deleted name still points at.
[[nodiscard]] std::vector<Ext4File> rootFiles();

// What lives under `photos`.
[[nodiscard]] std::vector<Ext4File> photosFiles();

// The inode on the orphan list: unlinked while still open, so no directory
// entry names it anywhere and only its content is recoverable.
[[nodiscard]] Ext4File orphanFile();

// The name of the deleted entry whose inode has since been handed to a live
// file. It is a name and nothing else — there is no content behind it any more,
// which is exactly what the walk has to notice.
inline constexpr std::string_view kReusedName = "taken.txt";

} // namespace revenant::imagegen::ext4
