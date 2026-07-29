// SPDX-License-Identifier: GPL-3.0-or-later
#include "WindowMatch.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/SignatureTable.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::carve {

namespace {

// The candidate's first byte for a magic found at device offset `absolute`.
// A magic sitting closer to the device start than its own in-file offset
// cannot belong to a real file there — and without this check the subtraction
// wraps and invents a candidate near the end of the address space.
std::optional<std::uint64_t> candidateStart(std::uint64_t absolute, std::size_t inFileOffset) {
	if (absolute < inFileOffset) {
		return std::nullopt;
	}
	return absolute - inFileOffset;
}

// Whether `entry`'s magic is really there, given that its first byte is.
bool magicIsAt(std::span<const std::byte> tail, const SignatureEntry& entry) {
	return tail.size() >= entry.magic.size() &&
		   std::ranges::equal(tail.first(entry.magic.size()), entry.magic);
}

void recordMatch(std::vector<Match>& matches, const SignatureEntry& entry, std::uint64_t absolute) {
	const auto start = candidateStart(absolute, entry.inFileOffset);
	if (start.has_value()) {
		matches.push_back(
			Match{
				.offset = start.value(),
				.carver = entry.carver,
				.carverIndex = entry.carverIndex});
	}
}

// One window position whose first byte something could start with: each
// candidate signature is compared in full, and the ones that are really there
// become matches.
void matchAt(
	std::span<const std::byte> tail,
	std::uint64_t absolute,
	const SignatureTable& table,
	std::vector<Match>& matches) {
	for (const SignatureEntry& entry : table.startingWith(tail.front())) {
		if (magicIsAt(tail, entry)) {
			recordMatch(matches, entry, absolute);
		}
	}
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

void sortCanonically(std::vector<Match>& matches) {
	// The key is taken by value rather than tied by reference: a projection
	// handing back references into the element it was given is a lifetime
	// question nobody should have to answer while reading a sort.
	std::ranges::sort(matches, {}, [](const Match& match) {
		return std::pair{match.offset, match.carverIndex};
	});
}

void matchWindow(
	std::span<const std::byte> window,
	std::uint64_t windowOffset,
	const SignatureTable& table,
	std::vector<Match>& matches) {
	matches.clear();
	std::size_t at = 0;
	// The whole loop is this one test: for almost every byte of almost every
	// device, nothing can start there and the answer is one load away. Walked
	// as a range rather than by index so no bounds check rides along.
	for (const std::byte value : window) {
		if (!table.none(value)) {
			matchAt(window.subspan(at), windowOffset + at, table, matches);
		}
		++at;
	}
	sortCanonically(matches);
}

Result<std::span<const std::byte>>
readWindow(BlockDevice& device, std::uint64_t offset, std::span<std::byte> buffer) {
	const auto got = device.readAt(offset, buffer);
	if (!got.hasValue()) {
		return got.error();
	}
	return std::span<const std::byte>{buffer.first(got.value())};
}

Result<WindowMatches>
readAndMatch(BlockDevice& device, const WindowRead& read, const CarverRegistry& registry) {
	const auto window = readWindow(device, read.cursor, read.window);
	if (!window.hasValue()) {
		return window.error();
	}
	std::vector<Match>& matches = *read.matches;
	matchWindow(window.value(), read.cursor, registry.signatureTable(), matches);
	const auto cursor = read.cursor;
	std::erase_if(matches, [cursor](const Match& match) { return match.offset < cursor; });
	return WindowMatches{
		.bytesRead = window.value().size(),
		.overlap = overlapBytes(registry, window.value().size()),
		.matches = matches};
}

} // namespace revenant::carve
