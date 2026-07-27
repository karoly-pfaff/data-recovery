// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/volume/PartitionView.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/core/io/ReadRange.hpp"

namespace revenant::volume {

namespace {

// clampLength's three arguments are always passed by name at a single call site
// (the PartitionView constructor), so the swap risk the check targets does not apply.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::uint64_t clampLength(std::uint64_t start, std::uint64_t length, std::uint64_t parentSize) {
	if (start >= parentSize) {
		return 0;
	}
	const auto available = parentSize - start;
	return std::min(length, available);
}

} // namespace

PartitionView::PartitionView(
	BlockDevice& parent,
	std::uint64_t start,
	std::uint64_t length) noexcept
	: parent_(parent), start_(start),
	  sizeInBytes_(clampLength(start, length, parent.sizeInBytes())) {}

std::uint64_t PartitionView::sizeInBytes() const {
	return sizeInBytes_;
}

std::uint32_t PartitionView::sectorSize() const {
	return parent_.sectorSize();
}

Result<std::size_t> PartitionView::readAt(std::uint64_t offset, std::span<std::byte> buffer) {
	if (offset > std::numeric_limits<std::uint64_t>::max() - start_) {
		return Error{.code = ErrorCode::kOverflow, .offset = offset};
	}
	const auto want = clampReadRange(offset, buffer.size(), sizeInBytes_);
	if (!want.hasValue()) {
		return want.error();
	}
	const auto parentOffset = start_ + offset;
	return parent_.readAt(parentOffset, buffer.first(want.value()));
}

} // namespace revenant::volume
