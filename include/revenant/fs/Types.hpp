// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace revenant::fs {

// A device-relative byte range. Used for non-resident file data extents.
struct Extent {
	std::uint64_t deviceOffset;
	std::uint64_t lengthBytes;
};

// Raw NTFS FILETIME values (100 ns ticks since 1601-01-01 UTC). Human-readable
// conversion is a reporting concern, not the parser's.
struct Timestamps {
	std::uint64_t created;
	std::uint64_t modified;
	std::uint64_t accessed;
};

// Lifecycle classification for a filesystem entry. Deleted entries are the
// undelete target; orphaned entries lost their parent chain.
enum class EntryState : std::uint8_t { kLive, kDeleted, kOrphaned };

} // namespace revenant::fs
