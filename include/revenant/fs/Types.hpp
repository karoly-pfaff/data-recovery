// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace revenant::fs {

// A device-relative byte range. Used for non-resident file data extents.
struct Extent {
	std::uint64_t deviceOffset;
	std::uint64_t lengthBytes;
};

// FILETIME ticks (100 ns since 1601-01-01 UTC) — the layer's one epoch, whatever
// filesystem produced them. It is the widest and finest of the four: DOS time
// and Unix seconds both convert into it without loss, and the reverse would not
// hold, so each parser converts on its way out. Human-readable conversion is a
// reporting concern, not the parser's.
struct Timestamps {
	std::uint64_t created;
	std::uint64_t modified;
	std::uint64_t accessed;
};

// Lifecycle classification for a filesystem entry. Deleted entries are the
// undelete target; orphaned entries lost their parent chain.
enum class EntryState : std::uint8_t { kLive, kDeleted, kOrphaned };

} // namespace revenant::fs
