// SPDX-License-Identifier: GPL-3.0-or-later
#include "ExtentLocate.hpp"

#include <cstdint>
#include <limits>
#include <span>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::fs {

namespace {

constexpr std::uint64_t kMaxOffset = std::numeric_limits<std::uint64_t>::max();

// `inner` is the range translated into one extent's own coordinates: it starts
// `inner.offset` bytes into `extent` and must end inside it.
[[nodiscard]] Result<std::uint64_t> withinExtent(const Extent& extent, FileRange inner) {
	if (inner.length > extent.lengthBytes - inner.offset) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = inner.offset};
	}
	if (inner.offset > kMaxOffset - extent.deviceOffset) {
		return Error{.code = ErrorCode::kOverflow, .offset = extent.deviceOffset};
	}
	return extent.deviceOffset + inner.offset;
}

// Finds the extent `range` starts in and hands it over in that extent's own
// coordinates. `consumed` never passes `range.offset` — the loop advances it
// only when the offset lies beyond this extent — so neither subtraction wraps.
[[nodiscard]] Result<std::uint64_t> findExtent(std::span<const Extent> extents, FileRange range) {
	std::uint64_t consumed = 0;
	for (const Extent& extent : extents) {
		if (range.offset - consumed < extent.lengthBytes) {
			const FileRange inner{.offset = range.offset - consumed, .length = range.length};
			return withinExtent(extent, inner);
		}
		consumed += extent.lengthBytes;
	}
	return Error{.code = ErrorCode::kOutOfRange, .offset = range.offset};
}

} // namespace

Result<std::uint64_t> locateInExtents(std::span<const Extent> extents, FileRange range) {
	if (range.length > kMaxOffset - range.offset) {
		return Error{.code = ErrorCode::kOverflow, .offset = range.offset};
	}
	return findExtent(extents, range);
}

} // namespace revenant::fs
