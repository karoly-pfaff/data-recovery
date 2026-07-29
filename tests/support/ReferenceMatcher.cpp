// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/ReferenceMatcher.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <vector>

#include "carve/WindowMatch.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/FormatCarver.hpp"
#include "revenant/carve/Signature.hpp"

namespace revenant::testing {

namespace {

using revenant::carve::Match;
using revenant::carve::Signature;

// Every position in `window` where `magic` occurs.
[[nodiscard]] std::vector<std::uint64_t>
findAll(std::span<const std::byte> window, std::span<const std::byte> magic) {
	std::vector<std::uint64_t> found;
	auto match = std::ranges::search(window, magic);
	while (!match.empty()) {
		found.push_back(static_cast<std::uint64_t>(std::distance(window.begin(), match.begin())));
		match = std::ranges::search(std::ranges::subrange(match.begin() + 1, window.end()), magic);
	}
	return found;
}

// The candidate's first byte for a magic found at device offset `absolute`.
[[nodiscard]] std::optional<std::uint64_t>
candidateStart(std::uint64_t absolute, const Signature& signature) {
	if (absolute < signature.offset) {
		return std::nullopt;
	}
	return absolute - signature.offset;
}

struct Window {
	std::span<const std::byte> bytes;
	std::uint64_t offset = 0;
};

void appendSignatureMatches(
	const Window& window,
	const Signature& signature,
	std::uint32_t carverIndex,
	const carve::FormatCarver& carver,
	std::vector<Match>& matches) {
	for (const auto at : findAll(window.bytes, signature.magic)) {
		const auto start = candidateStart(window.offset + at, signature);
		if (start.has_value()) {
			matches.push_back(
				Match{.offset = start.value(), .carver = &carver, .carverIndex = carverIndex});
		}
	}
}

void appendMatches(
	const Window& window,
	std::uint32_t carverIndex,
	const carve::FormatCarver& carver,
	std::vector<Match>& matches) {
	for (const Signature& signature : carver.signatures()) {
		appendSignatureMatches(window, signature, carverIndex, carver, matches);
	}
}

} // namespace

std::vector<Match> referenceMatches(
	std::span<const std::byte> window,
	std::uint64_t windowOffset,
	const carve::CarverRegistry& registry) {
	const Window whole{.bytes = window, .offset = windowOffset};
	std::vector<Match> matches;
	std::uint32_t carverIndex = 0;
	for (const std::unique_ptr<carve::FormatCarver>& carver : registry.carvers()) {
		appendMatches(whole, carverIndex, *carver, matches);
		++carverIndex;
	}
	carve::sortCanonically(matches);
	return matches;
}

} // namespace revenant::testing
