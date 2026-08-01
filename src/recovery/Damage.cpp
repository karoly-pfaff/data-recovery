// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery/Damage.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include "revenant/core/io/BadRange.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::recovery {

namespace {

// One extent, restated where the map can see it. Every comparison below is
// half-open — `[offset, offset + length)` — so a range ending on an extent's
// first byte overlaps it by one and one byte earlier overlaps by none.
struct Span {
	std::uint64_t begin = 0;
	std::uint64_t end = 0;
};

// `a + b`, saturating rather than wrapping. Both operands come off a hostile
// device — an extent's offset and length are decoded from on-disk metadata, and
// a scoped run's zero is a partition entry's offset — and a wrap here would turn
// "this whole file is damaged" into "no damage at all", which is the one answer
// this must never give. Saturating instead names a range past the end of any
// device, which intersects nothing and is therefore merely useless.
[[nodiscard]] std::uint64_t saturatingSum(std::uint64_t a, std::uint64_t b) noexcept {
	constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();
	return b > kMax - a ? kMax : a + b;
}

[[nodiscard]] Span placedOn(const fs::Extent& extent, std::uint64_t startBytes) {
	const auto begin = saturatingSum(startBytes, extent.deviceOffset);
	return Span{.begin = begin, .end = saturatingSum(begin, extent.lengthBytes)};
}

[[nodiscard]] Span spanOf(const BadRange& range) {
	return Span{
		.begin = range.offsetBytes,
		.end = saturatingSum(range.offsetBytes, range.lengthBytes)};
}

// What the two have in common, or nothing.
[[nodiscard]] std::optional<BadRange> overlap(const Span& extent, const Span& damage) {
	const auto begin = std::max(extent.begin, damage.begin);
	const auto end = std::min(extent.end, damage.end);
	if (begin >= end) {
		return std::nullopt;
	}
	return BadRange{.offsetBytes = begin, .lengthBytes = end - begin};
}

// Everything one extent has in common with the map, in map order.
[[nodiscard]] std::vector<BadRange>
overlapsIn(const Span& extent, std::span<const BadRange> damage) {
	std::vector<BadRange> found;
	for (const BadRange& range : damage) {
		const auto shared = overlap(extent, spanOf(range));
		if (shared.has_value()) {
			found.push_back(shared.value());
		}
	}
	return found;
}

} // namespace

std::vector<BadRange> inventedIn(
	std::span<const fs::Extent> extents,
	std::span<const BadRange> damage,
	std::uint64_t startBytes) {
	std::vector<BadRange> found;
	for (const fs::Extent& extent : extents) {
		const auto shared = overlapsIn(placedOn(extent, startBytes), damage);
		found.insert(found.end(), shared.begin(), shared.end());
	}
	return found;
}

} // namespace revenant::recovery
