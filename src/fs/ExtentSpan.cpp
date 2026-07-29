// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/ExtentSpan.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::fs {

namespace {

[[nodiscard]] bool continues(const Extent& extent, const Extent& run) noexcept {
	return extent.deviceOffset + extent.lengthBytes == run.deviceOffset;
}

// Appends `extent` shortened to what the file still needs, and reports what is
// still missing after it. A fully consumed size drops everything after.
[[nodiscard]] std::uint64_t
takeExtent(std::vector<Extent>& taken, const Extent& extent, std::uint64_t remaining) {
	const auto take = std::min(extent.lengthBytes, remaining);
	if (take == 0) {
		return 0;
	}
	taken.push_back(Extent{.deviceOffset = extent.deviceOffset, .lengthBytes = take});
	return remaining - take;
}

} // namespace

void appendExtent(std::vector<Extent>& extents, const Extent& run) {
	if (!extents.empty() && continues(extents.back(), run)) {
		extents.back().lengthBytes += run.lengthBytes;
		return;
	}
	extents.push_back(run);
}

std::uint64_t extentBytes(std::span<const Extent> extents) noexcept {
	std::uint64_t total = 0;
	for (const Extent& extent : extents) {
		total += extent.lengthBytes;
	}
	return total;
}

Result<std::vector<Extent>> trimToSize(std::span<const Extent> extents, std::uint64_t sizeBytes) {
	std::vector<Extent> taken;
	std::uint64_t remaining = sizeBytes;
	for (const Extent& extent : extents) {
		remaining = takeExtent(taken, extent, remaining);
	}
	if (remaining != 0) {
		return Error{.code = ErrorCode::kInvalidArgument, .offset = sizeBytes};
	}
	return taken;
}

} // namespace revenant::fs
