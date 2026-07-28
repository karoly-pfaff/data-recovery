// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. FAT states time as a pair of packed 16-bit DOS fields; the
// filesystem layer speaks FILETIME ticks. This is the only place that
// conversion lives. Not a public interface.

#include <cstdint>

namespace revenant::fs::fat {

// A DOS date/time pair as it sits in a directory entry. The date packs year
// (since 1980), month and day into 7/4/5 bits; the time packs hours, minutes
// and *two-second* units into 5/6/5.
struct DosTimestamp {
	std::uint16_t date;
	std::uint16_t time;
};

// FILETIME ticks (100 ns since 1601-01-01 UTC) for `stamp`, or 0 when any
// field falls outside what DOS can express. Zero means "no timestamp", which
// is the honest answer for a field that cannot be read — a fabricated date
// would be worse than none. An all-zero pair (the "never set" encoding) lands
// there by the same rule, since month 0 is not a month.
[[nodiscard]] std::uint64_t toFiletime(DosTimestamp stamp) noexcept;

} // namespace revenant::fs::fat
