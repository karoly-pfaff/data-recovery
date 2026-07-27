// SPDX-License-Identifier: GPL-3.0-or-later
#include "WindowMatch.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
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

// A window of device bytes together with where it sits on the device: the two
// travel as one value so no caller can pair the wrong offset with a window.
struct Window {
	std::span<const std::byte> bytes;
	std::uint64_t offset = 0;
};

// Every position in `window` where `magic` occurs.
std::vector<std::uint64_t>
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
// A magic sitting closer to the device start than its own in-file offset
// cannot belong to a real file there — and without this check the subtraction
// wraps and invents a candidate near the end of the address space.
std::optional<std::uint64_t> candidateStart(std::uint64_t absolute, const Signature& signature) {
	if (absolute < signature.offset) {
		return std::nullopt;
	}
	return absolute - signature.offset;
}

void appendSignatureMatches(
	const Window& window,
	const Signature& signature,
	const FormatCarver& carver,
	std::vector<Match>& matches) {
	for (const auto at : findAll(window.bytes, signature.magic)) {
		const auto start = candidateStart(window.offset + at, signature);
		if (start.has_value()) {
			matches.push_back(Match{.offset = start.value(), .carver = &carver});
		}
	}
}

void appendMatches(const Window& window, const FormatCarver& carver, std::vector<Match>& matches) {
	for (const Signature& signature : carver.signatures()) {
		appendSignatureMatches(window, signature, carver, matches);
	}
}

std::vector<Match> matchesInWindow(const Window& window, const CarverRegistry& registry) {
	std::vector<Match> matches;
	for (const auto& carver : registry.carvers()) {
		appendMatches(window, *carver, matches);
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
	auto matches = matchesInWindow(Window{.bytes = window.value(), .offset = cursor}, registry);
	std::erase_if(matches, [cursor](const Match& match) { return match.offset < cursor; });
	return WindowMatches{
		.bytesRead = window.value().size(),
		.overlap = overlapBytes(registry, window.value().size()),
		.matches = std::move(matches)};
}

} // namespace revenant::carve
