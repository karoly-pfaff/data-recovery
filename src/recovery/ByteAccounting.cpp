// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/ByteAccounting.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::recovery {

namespace {

using carve::ScanRegion;
using fs::Extent;

// One past the region's last byte, saturating: a length read off a disk may
// not wrap the bound it is supposed to impose.
[[nodiscard]] std::uint64_t endOf(const Extent& region) noexcept {
	constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();
	return region.lengthBytes > kMax - region.deviceOffset
			   ? kMax
			   : region.deviceOffset + region.lengthBytes;
}

// Fuses `region` into the last one when they overlap or touch, else starts a
// new one. `fused` is built in offset order, so only its last entry can ever
// be a neighbour.
void appendFused(std::vector<Extent>& fused, const Extent& region) {
	if (fused.empty() || region.deviceOffset > endOf(fused.back())) {
		fused.push_back(region);
		return;
	}
	const auto reach = std::max(endOf(fused.back()), endOf(region));
	fused.back().lengthBytes = reach - fused.back().deviceOffset;
}

// The complement being built: the gaps emitted so far, and how far along the
// device the walk has consumed.
struct Complement {
	std::vector<ScanRegion> gaps;
	std::uint64_t cursor;
};

// Emits the gap between the cursor and `upTo`, then moves the cursor there.
// Nothing is emitted when the two do not straddle any bytes.
void takeUpTo(Complement& complement, std::uint64_t upTo) {
	if (upTo <= complement.cursor) {
		return;
	}
	complement.gaps.push_back(
		ScanRegion{.offset = complement.cursor, .lengthBytes = upTo - complement.cursor});
	complement.cursor = upTo;
}

// Consumes one accounted region: emits the gap in front of it, then skips the
// region itself. Regions arrive fused and in order, so the cursor only ever
// moves forward.
void consume(Complement& complement, const Extent& region, std::uint64_t deviceSize) {
	takeUpTo(complement, std::min(region.deviceOffset, deviceSize));
	complement.cursor = std::max(complement.cursor, std::min(endOf(region), deviceSize));
}

} // namespace

void ByteAccounting::addRegion(const Extent& extent) {
	if (accounted_.size() >= kMaxAccountedRegions) {
		++dropped_;
		return;
	}
	accounted_.push_back(extent);
}

void ByteAccounting::account(const fs::RecoveredEntry& entry) {
	if (entry.recoverability != Confidence::kValid) {
		return;
	}
	for (const Extent& extent : entry.extents) {
		addRegion(extent);
	}
}

std::vector<Extent> ByteAccounting::fusedRegions() const {
	auto sorted = accounted_;
	std::ranges::sort(sorted, {}, &Extent::deviceOffset);
	std::vector<Extent> fused;
	for (const Extent& region : sorted) {
		appendFused(fused, region);
	}
	return fused;
}

std::vector<ScanRegion> ByteAccounting::gaps(std::uint64_t deviceSize) const {
	Complement complement{.gaps = {}, .cursor = 0};
	for (const Extent& region : fusedRegions()) {
		consume(complement, region, deviceSize);
	}
	takeUpTo(complement, deviceSize);
	return std::move(complement.gaps);
}

std::uint64_t ByteAccounting::accountedBytes() const {
	const auto fused = fusedRegions();
	return std::accumulate(
		fused.begin(),
		fused.end(),
		std::uint64_t{0},
		[](std::uint64_t sum, const Extent& region) { return sum + region.lengthBytes; });
}

std::uint64_t ByteAccounting::droppedRegions() const noexcept {
	return dropped_;
}

} // namespace revenant::recovery
