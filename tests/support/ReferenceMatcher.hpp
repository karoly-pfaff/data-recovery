// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "carve/WindowMatch.hpp"
#include "revenant/carve/CarverRegistry.hpp"

namespace revenant::testing {

// The matcher story-0107 shipped and story-0502 replaced: for every carver, for
// every one of its signatures, a full `std::ranges::search` over the whole
// window, restarted after each hit.
//
// It is kept because correctness here is not "the carve tests still pass" —
// those exercise a handful of planted headers. It is "the same `Match` sequence
// for any bytes at all", and the only honest way to assert that is against an
// implementation whose simplicity makes it obviously right. Its ordering comes
// from `sortCanonically`, the same total order the production matcher uses, so
// what the differential test compares is which hits were found and not which
// sort happened to run.
[[nodiscard]] std::vector<carve::Match> referenceMatches(
	std::span<const std::byte> window,
	std::uint64_t windowOffset,
	const carve::CarverRegistry& registry);

} // namespace revenant::testing
