// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The small vocabulary the ext4 directory walk moves through: where it
// is, how far one record carried it, and the two names that are a directory's
// own place rather than files in it. Not a public interface.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "fs/ext4/EntryFromInode.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::fs::ext4 {

// Where the walk is.
struct Cursor {
	std::string path;
	std::uint32_t inode;
	unsigned depth;
};

// How far a record carried the walk, and how many entries came out of it. A
// zero distance ends the block: without a usable record length there is nowhere
// to go next but a guess.
struct RecordStep {
	std::size_t bytes;
	std::uint64_t reported;
};

// Where a walk over one directory block has got to, and what it has reported so
// far. Both move together, one record at a time.
struct BlockCursor {
	std::size_t at;
	std::uint64_t reported;
};

// `.` and `..`. Every ext4 directory begins with both; neither is a file in it,
// and following `..` would send the walk climbing back the way it came.
[[nodiscard]] inline bool isSelfOrParent(std::span<const std::byte> name) {
	return !name.empty() && name.size() <= 2 &&
		   std::ranges::all_of(name, [](std::byte raw) { return raw == std::byte{'.'}; });
}

// One name the walk found, decoded and placed. ext4 enforces no encoding, so
// what the bytes mean is `fs::decodeRawName`'s question and whether they
// survived it intact is the entry's grade (ADR-0010).
[[nodiscard]] FoundName foundName(
	const Cursor& cursor,
	std::span<const std::byte> raw,
	std::uint32_t number,
	EntryState state);

// An orphan has no directory entry anywhere on the volume, so it has no name to
// recover: its inode number is the only thing that identifies it. Where such a
// file is *written* is the sink's policy, not this parser's.
[[nodiscard]] inline FoundName orphanName(std::uint32_t number) {
	return FoundName{
		.path = "#" + std::to_string(number),
		.inodeNumber = number,
		.state = EntryState::kOrphaned,
		.nameIsExact = false};
}

} // namespace revenant::fs::ext4
