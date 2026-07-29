// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/ext4/WalkCursor.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include "fs/DirectoryTreeWalk.hpp"
#include "fs/ext4/EntryFromInode.hpp"
#include "revenant/fs/NameDecode.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::fs::ext4 {

FoundName foundName(
	const Cursor& cursor,
	std::span<const std::byte> raw,
	std::uint32_t number,
	EntryState state) {
	const auto decoded = decodeRawName(raw);
	return FoundName{
		.path = joinedPath(cursor.path, decoded.utf8),
		.inodeNumber = number,
		.state = state,
		.nameIsExact = decoded.lossless};
}

} // namespace revenant::fs::ext4
