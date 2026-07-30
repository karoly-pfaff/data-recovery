// SPDX-License-Identifier: GPL-3.0-or-later
#include "WindowMatch.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "PrefilterAvx2.hpp"
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

// Where matches go and what decides them, bundled so the survivor walk stays
// inside the five-parameter limit without taking two loose pointers.
struct MatchTarget {
	const SignatureTable* table = nullptr; // non-owning, never null
	std::vector<Match>* matches = nullptr; // non-owning, never null
};

// Every position, one byte at a time. The whole loop is the one test: for
// almost every byte of almost every device, nothing can start there and the
// answer is one load away. Walked as a range rather than by index so no bounds
// check rides along.
void matchPortable(
	std::span<const std::byte> window,
	std::uint64_t windowOffset,
	const SignatureTable& table,
	std::vector<Match>& matches) {
	std::size_t at = 0;
	for (const std::byte value : window) {
		if (!table.none(value)) {
			matchAt(window.subspan(at), windowOffset + at, table, matches);
		}
		++at;
	}
}

// One vector of the window: its bytes, where they sit on the device, and which
// of its positions the prefilter kept. Bundled because a bare offset beside a
// bare mask is two integers a caller could hand over the wrong way round.
struct FilteredVector {
	std::span<const std::byte> bytes;
	std::uint64_t offset = 0;
	std::uint32_t surviving = 0;
};

// The positions that mask kept, each still asked the exact question the portable
// path asks. The prefilter only narrows *which* positions are asked.
void matchSurvivors(const FilteredVector& vector, const MatchTarget& into) {
	auto surviving = vector.surviving;
	while (surviving != 0) {
		const auto at = static_cast<std::size_t>(std::countr_zero(surviving));
		matchAt(vector.bytes.subspan(at), vector.offset + at, *into.table, *into.matches);
		surviving &= surviving - 1;
	}
}

// One batch: the masks for its vectors, then the survivors each one kept.
void matchBatch(
	std::span<const std::byte> window,
	std::uint64_t windowOffset,
	const MatchTarget& into) {
	const auto masks = survivorsAvx2(window, into.table->nibbleFilter());
	for (std::size_t vector = 0; vector < masks.size(); ++vector) {
		const auto from = vector * kPrefilterVectorBytes;
		matchSurvivors(
			FilteredVector{
				.bytes = window.subspan(from),
				.offset = windowOffset + from,
				.surviving = masks.at(vector)},
			into);
	}
}

// The same pass, a batch of vectors at a time, with the tail handed back to the
// portable walk: bytes that do not fill a batch have nothing to vectorize.
void matchWithFastPath(
	std::span<const std::byte> window,
	std::uint64_t windowOffset,
	const SignatureTable& table,
	std::vector<Match>& matches) {
	const MatchTarget into{.table = &table, .matches = &matches};
	std::size_t at = 0;
	while (window.size() - at >= kPrefilterChunkBytes) {
		matchBatch(window.subspan(at), windowOffset + at, into);
		at += kPrefilterChunkBytes;
	}
	matchPortable(window.subspan(at), windowOffset + at, table, matches);
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
	// Chosen once per window, not once per byte, and from a value the table
	// settled when it was built rather than from a question asked of the CPU
	// here.
	if (table.usesFastPath()) {
		matchWithFastPath(window, windowOffset, table, matches);
	} else {
		matchPortable(window, windowOffset, table, matches);
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
