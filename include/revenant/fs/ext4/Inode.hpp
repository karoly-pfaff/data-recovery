// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::fs::ext4 {

// The smallest inode ext2 ever wrote, and still the size of an inode's fixed
// part. Anything an ext4 volume adds beyond it lives in the extra area, whose
// extent the inode itself declares.
inline constexpr std::size_t kMinInodeBytes = 128;

// `i_block`: 60 bytes that are either an extent tree or ext2's indirect block
// list, depending on the inode's own flags.
inline constexpr std::size_t kBlockMapBytes = 60;

// One inode, as far as recovery cares. The uid, gid, generation and ACL fields
// beside these describe *permission*, which is a restore concern rather than a
// discovery one, and this layer only discovers (ADR-0006).
struct Ext4Inode {
	std::uint16_t mode{};
	std::uint16_t linkCount{};
	std::uint64_t sizeInBytes{};
	std::uint32_t flags{};

	// `i_dtime`, raw. On a freed inode it is when the volume freed it. On an
	// inode still on the *orphan list* it is not a time at all — it is the next
	// orphan's number, which is how that list is chained. Reported unread so
	// whoever knows which it is can say.
	std::uint32_t deletionTime{};

	Timestamps timestamps{};
	std::array<std::byte, kBlockMapBytes> blockMap{};

	bool isDirectory{};
	bool isRegularFile{};

	// Whether `blockMap` holds an extent tree rather than ext2's indirect block
	// list. This build reads only extent trees; an inode without the flag has
	// its content left to the carve pass rather than guessed at.
	bool usesExtents{};

	// Nothing links to this inode any more — the file it held was unlinked, or
	// it is on the orphan list waiting to be freed.
	bool isDeleted{};

	// The inode has never held anything: no type in its mode at all. A *deleted*
	// inode keeps its mode, its size and its times, which is what makes undelete
	// possible, so the two must not be confused.
	bool isUnused{};
};

// Parses one inode out of `slot`, which must be at least the fixed 128 bytes.
//
// Truncated input yields kOutOfRange. Nothing else is rejected: an inode with a
// nonsense mode or an impossible size is a *damaged* inode, and the walk's job
// is to grade it, not this parser's to refuse it.
//
// `sizeInBytes` folds in `i_size_high` only for a regular file: ext4 reuses that
// field as `i_dir_acl` for everything else, and reading it unconditionally would
// give a directory a size in the terabytes off two bytes of unrelated data.
//
// `timestamps.created` comes from `i_crtime`, which only exists when the inode
// is large enough and says so through `i_extra_isize`; it stays zero when it is
// not there. `i_ctime` is when the *inode* last changed, not when the file was
// made, so it is never substituted for it.
[[nodiscard]] Result<Ext4Inode> parseExt4Inode(std::span<const std::byte> slot);

} // namespace revenant::fs::ext4
