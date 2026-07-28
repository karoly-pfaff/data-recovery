// SPDX-License-Identifier: GPL-3.0-or-later
// Internal. Turns a byte offset inside an extent-mapped file into the device
// offset holding it. Shared by every extent-based filesystem, so it lives in
// `fs/` rather than under one of them. Not a public interface.
#pragma once

#include <cstdint>
#include <span>

#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::fs {

// A byte range within a file, in file order — the coordinates a caller has
// before knowing where on the device the bytes live.
struct FileRange {
	std::uint64_t offset;
	std::uint64_t length;
};

// The device offset of `range` within a file laid out as `extents` (file order,
// not device order). A range that reaches past the last extent is kOutOfRange,
// and one that spans an extent boundary is kInvalidArgument: two runs are two
// device reads, and answering with the first run's offset would hand back
// whatever happens to follow it on disk.
[[nodiscard]] Result<std::uint64_t>
locateInExtents(std::span<const Extent> extents, FileRange range);

} // namespace revenant::fs
