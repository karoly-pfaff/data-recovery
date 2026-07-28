// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/Arbitration.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <numeric>
#include <tuple>
#include <utility>
#include <vector>

#include "revenant/core/Confidence.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/recovery/Candidate.hpp"
#include "revenant/recovery/CandidateIndex.hpp"
#include "revenant/recovery/RegionSet.hpp"

namespace revenant::recovery {

namespace {

// Where a candidate begins on the device. Content that lives inside its own
// metadata record claims no region, so it sorts to the front and blocks
// nothing.
[[nodiscard]] std::uint64_t startOf(const Candidate& candidate) noexcept {
	return candidate.extents.empty() ? 0 : candidate.extents.front().deviceOffset;
}

[[nodiscard]] std::uint64_t totalBytes(const Candidate& candidate) noexcept {
	return std::accumulate(
		candidate.extents.begin(),
		candidate.extents.end(),
		static_cast<std::uint64_t>(candidate.residentContent.size()),
		[](std::uint64_t sum, const fs::Extent& extent) { return sum + extent.lengthBytes; });
}

// The order candidates are offered in. Source comes first, and deliberately
// ahead of confidence: the two scales do not mean the same thing. A carver's
// verdict grades the *structure* of bytes it can see, while a filesystem entry
// knows the file's name, its timestamps, and — decisively — which runs its
// content is spread across. A carve starting at a fragmented file's first run
// would hand back garbage however perfect it looked, so a named entry wins its
// region outright rather than on a tie. Then most trusted, then position, then
// size, so every tie is broken by something and two runs over one device agree.
[[nodiscard]] auto rankOf(const Candidate& candidate) {
	constexpr auto kWidest = std::numeric_limits<std::uint64_t>::max();
	return std::tuple{
		static_cast<int>(candidate.source),
		-static_cast<int>(candidate.confidence),
		startOf(candidate),
		kWidest - totalBytes(candidate)};
}

[[nodiscard]] bool isBlocked(const RegionSet& claimed, const Candidate& candidate) {
	return std::ranges::any_of(candidate.extents, [&claimed](const fs::Extent& extent) {
		return claimed.overlaps(extent);
	});
}

void claimAll(RegionSet& claimed, const Candidate& candidate) {
	for (const fs::Extent& extent : candidate.extents) {
		claimed.add(extent);
	}
}

// A candidate wins whole or not at all.
void acceptOrSuppress(Arbitration& result, RegionSet& claimed, Candidate candidate) {
	if (candidate.confidence == Confidence::kRejected || isBlocked(claimed, candidate)) {
		++result.suppressed;
		return;
	}
	claimAll(claimed, candidate);
	result.winners.push_back(std::move(candidate));
}

} // namespace

Arbitration arbitrate(std::vector<Candidate> candidates) {
	std::ranges::sort(candidates, {}, rankOf);
	Arbitration result{.winners = {}, .suppressed = 0};
	RegionSet claimed;
	for (Candidate& candidate : candidates) {
		acceptOrSuppress(result, claimed, std::move(candidate));
	}
	std::ranges::sort(result.winners, {}, startOf);
	return result;
}

Result<Arbitration> arbitrateIndex(const std::filesystem::path& directory) {
	auto contents = readIndex(directory);
	if (!contents.hasValue()) {
		return contents.error();
	}
	return arbitrate(std::move(contents.value().candidates));
}

} // namespace revenant::recovery
