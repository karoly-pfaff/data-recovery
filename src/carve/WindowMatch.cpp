// SPDX-License-Identifier: GPL-3.0-or-later
#include "WindowMatch.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/FormatCarver.hpp"
#include "revenant/carve/Signature.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::carve {

namespace {

void appendMatches(
	std::span<const std::byte> window,
	std::uint64_t windowOffset,
	const FormatCarver& carver,
	std::vector<Match>& matches) {
	for (const Signature& signature : carver.signatures()) {
		auto found = std::ranges::search(window, signature.magic);
		while (!found.empty()) {
			const auto at =
				static_cast<std::uint64_t>(std::distance(window.begin(), found.begin()));
			matches.push_back(
				Match{.offset = windowOffset + at - signature.offset, .carver = &carver});
			found = std::ranges::search(
				std::ranges::subrange(found.begin() + 1, window.end()),
				signature.magic);
		}
	}
}

std::vector<Match> matchesInWindow(
	std::span<const std::byte> window,
	std::uint64_t windowOffset,
	const CarverRegistry& registry) {
	std::vector<Match> matches;
	for (const auto& carver : registry.carvers()) {
		appendMatches(window, windowOffset, *carver, matches);
	}
	std::ranges::sort(matches, {}, &Match::offset);
	return matches;
}

// The cross-window overlap: the widest signature span minus one byte, so a
// magic straddling a window boundary is always whole in the next window.
// Derived only from registered signatures (ADR-0009), never device data.
std::size_t overlapBytes(const CarverRegistry& registry, std::size_t bytesRead) {
	if (registry.maxSignatureBytes() == 0) {
		return 0;
	}
	return std::min(registry.maxSignatureBytes() - 1, bytesRead);
}

} // namespace

Result<std::span<const std::byte>>
readWindow(BlockDevice& device, std::uint64_t offset, std::span<std::byte> buffer) {
	const auto got = device.readAt(offset, buffer);
	if (!got.hasValue()) {
		return got.error();
	}
	return std::span<const std::byte>{buffer.first(got.value())};
}

Result<WindowMatches> readAndMatch(
	BlockDevice& device,
	std::uint64_t cursor,
	std::span<std::byte> windowBuffer,
	const CarverRegistry& registry) {
	const auto window = readWindow(device, cursor, windowBuffer);
	if (!window.hasValue()) {
		return window.error();
	}
	auto matches = matchesInWindow(window.value(), cursor, registry);
	std::erase_if(matches, [cursor](const Match& match) { return match.offset < cursor; });
	return WindowMatches{
		.bytesRead = window.value().size(),
		.overlap = overlapBytes(registry, window.value().size()),
		.matches = std::move(matches)};
}

} // namespace revenant::carve
