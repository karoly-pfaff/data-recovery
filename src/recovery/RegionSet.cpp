// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/RegionSet.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <utility>
#include <vector>

#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::recovery {

namespace {

using carve::ScanRegion;
using fs::Extent;
using Regions = std::map<std::uint64_t, std::uint64_t>;

// One past the region's last byte, saturating: a length read off a disk may
// not wrap the bound it is supposed to impose.
[[nodiscard]] std::uint64_t endOf(const Extent& region) noexcept {
	constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();
	return region.lengthBytes > kMax - region.deviceOffset
			   ? kMax
			   : region.deviceOffset + region.lengthBytes;
}

// A half-open device range, mid-fuse.
struct Span {
	std::uint64_t start;
	std::uint64_t finish;
};

// The first claimed region a span starting at `start` could reach: the
// predecessor when it stretches that far, otherwise the first one past it.
[[nodiscard]] Regions::iterator firstTouching(Regions& regions, std::uint64_t start) {
	auto after = regions.upper_bound(start);
	if (after != regions.begin() && std::prev(after)->second >= start) {
		return std::prev(after);
	}
	return after;
}

// Erases every region the span reaches from `at` onwards, widening the span
// over each one. Touching end to end counts: two adjacent claims are one.
[[nodiscard]] Span absorbFrom(Regions& regions, Regions::iterator at, Span span) {
	while (at != regions.end() && at->first <= span.finish) {
		span.start = std::min(span.start, at->first);
		span.finish = std::max(span.finish, at->second);
		at = regions.erase(at);
	}
	return span;
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

} // namespace

void RegionSet::add(const Extent& region) {
	if (region.lengthBytes == 0) {
		return;
	}
	const Span claimed{.start = region.deviceOffset, .finish = endOf(region)};
	const auto fused = absorbFrom(regions_, firstTouching(regions_, claimed.start), claimed);
	regions_[fused.start] = fused.finish;
}

bool RegionSet::overlaps(const Extent& region) const {
	if (region.lengthBytes == 0) {
		return false;
	}
	const auto after = regions_.upper_bound(region.deviceOffset);
	if (after != regions_.begin() && std::prev(after)->second > region.deviceOffset) {
		return true;
	}
	return after != regions_.end() && after->first < endOf(region);
}

std::vector<ScanRegion> RegionSet::gaps(std::uint64_t deviceSize) const {
	Complement complement{.gaps = {}, .cursor = 0};
	for (const auto& [start, finish] : regions_) {
		takeUpTo(complement, std::min(start, deviceSize));
		complement.cursor = std::max(complement.cursor, std::min(finish, deviceSize));
	}
	takeUpTo(complement, deviceSize);
	return std::move(complement.gaps);
}

std::uint64_t RegionSet::claimedBytes() const noexcept {
	return std::accumulate(
		regions_.begin(),
		regions_.end(),
		std::uint64_t{0},
		[](std::uint64_t sum, const Regions::value_type& region) {
			return sum + (region.second - region.first);
		});
}

std::size_t RegionSet::regionCount() const noexcept {
	return regions_.size();
}

} // namespace revenant::recovery
