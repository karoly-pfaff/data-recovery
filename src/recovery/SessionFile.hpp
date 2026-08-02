// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. How a file in the session directory is replaced whole-or-nothing:
// written beside its target and renamed over it. The checkpoint and the
// manifest both need it, and for the same reason — a replacement interrupted or
// refused halfway must leave the previous whole file, not a truncated one. Not
// a public interface.

#include <filesystem>
#include <string_view>

#include "revenant/core/Result.hpp"

namespace revenant::recovery {

// `text` written into `directory / name`, via a pending file renamed over it.
//
// The rename is what makes the replacement atomic; the pending write is what
// runs out of room, so a destination that filled up leaves the previous file
// standing rather than a half-written one.
//
// It takes text rather than bytes because one of its two callers writes a
// manifest that grows with the winner set — thousands of artifacts on a real
// volume — and a stream written one `put()` at a time costs more instructions
// than the enumeration that produced it. The checkpoint's sixty-four bytes
// convert to text for free. Measured: writing the manifest byte-wise cost 27%
// more instructions on the `ntfs-enumerate` benchmark.
//
// It takes the finished path rather than a directory and a name: those two
// would be adjacent strings, and writing a manifest's own text into a file
// named after it is not a mistake worth leaving available.
[[nodiscard]] Result<std::filesystem::path>
replaceFile(const std::filesystem::path& target, std::string_view text);

} // namespace revenant::recovery
