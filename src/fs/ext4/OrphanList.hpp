// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The volume's orphan list: inodes that were unlinked while still
// open, which the filesystem meant to free and may not have got to. No directory
// entry points at any of them, so there is no name to recover — only content.
// Not a public interface.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fs/ext4/InodeTable.hpp"

namespace revenant::fs::ext4 {

// A list longer than this is not a list, it is a chain built to be followed
// forever (ADR-0009).
inline constexpr std::size_t kMaxOrphans = 4096;

// The inodes on the list `head` begins, in the order it links them.
//
// The chain runs through each orphan's `i_dtime` — on an inode still on the
// list that field is not a time at all but the next orphan's number. Every link
// is a number off a disk, so the walk is bounded in length, refuses a number the
// volume could not have, and never visits an inode twice: a chain that points
// back into itself ends there rather than running forever.
[[nodiscard]] std::vector<std::uint32_t>
orphanInodes(const Ext4InodeTable& inodes, std::uint32_t head);

} // namespace revenant::fs::ext4
