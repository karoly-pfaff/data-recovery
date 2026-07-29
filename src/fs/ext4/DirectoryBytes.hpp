// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. A directory's own content, fetched. An ext4 directory is a file like
// any other — an inode with an extent tree — so reading one is two steps that
// have nothing to do with what its bytes mean. Not a public interface.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fs/ext4/EntryFromInode.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs::ext4 {

// A directory bigger than this is read up to the cap and no further: what a
// volume claims about the size of its own directories is data like any other
// (ADR-0009).
inline constexpr std::size_t kMaxDirectoryBytes = 4U << 20U;

// Every byte of the directory inode `number` names, up to the cap. The inode's
// own failures travel out unchanged — a directory whose tree will not map is
// unreadable, not empty.
[[nodiscard]] Result<std::vector<std::byte>>
readDirectoryBytes(const EntrySource& source, std::uint32_t number);

} // namespace revenant::fs::ext4
