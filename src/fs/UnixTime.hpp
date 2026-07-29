// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The last step of every timestamp conversion in the layer: seconds
// since the Unix epoch into the FILETIME ticks `fs::Timestamps` is stated in.
// ext4 reads those seconds straight off an inode; FAT and exFAT arrive at them
// by unpacking a DOS date. Where the FILETIME epoch sits is written here once.
// Not a public interface.

#include <cstdint>

namespace revenant::fs {

// FILETIME ticks (100 ns since 1601-01-01 UTC) for `seconds`.
//
// Zero yields zero: the layer spells "no timestamp" that way, and a field that
// was never set is exactly what a zero-seconds timestamp is on disk. A
// fabricated 1970-01-01 would be worse than none.
[[nodiscard]] std::uint64_t filetimeFromUnixSeconds(std::int64_t seconds) noexcept;

} // namespace revenant::fs
