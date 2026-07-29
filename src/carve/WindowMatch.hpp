// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to SignatureScanner's window loop — NOT a public interface. Not
// part of story-0107's produced Interfaces block; consumed only by
// SignatureScanner.cpp and WindowMatch.cpp in this directory, and by the
// reference matcher the differential test compares against. Subject to
// change without notice from outside src/carve/.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/FormatCarver.hpp"
#include "revenant/carve/SignatureTable.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::carve {

// A signature hit inside one window, in device coordinates.
struct Match {
	std::uint64_t offset;
	const FormatCarver* carver;
	// The carver's registration position, carried only to order two candidates
	// that start at the same byte. Without it that order is whatever the walk
	// happened to produce, and the walk is exactly what story-0502 changed.
	std::uint32_t carverIndex;

	friend bool operator==(const Match&, const Match&) = default;
};

// The order every matcher must produce: by candidate offset, and where two
// candidates start at the same byte, by the order their carvers were
// registered. Everything downstream depends on it — `processMatches` walks
// matches in order and skips those falling inside an extent a previous
// candidate resumed past — so a matcher emitting in a different order silently
// changes which candidates get carved. A *total* order rather than a sort by
// offset alone, because ties would otherwise be settled by the walk.
void sortCanonically(std::vector<Match>& matches);

// Every signature hit in `window`, in canonical order, in device coordinates.
// One pass over the window regardless of how many signatures are registered:
// each position asks the registry's table which signatures could begin with
// the byte there, and the common answer is none. `matches` is cleared and
// refilled, so its capacity is reused across windows.
void matchWindow(
	std::span<const std::byte> window,
	std::uint64_t windowOffset,
	const SignatureTable& table,
	std::vector<Match>& matches);

// Matches found in one window read, already filtered to that window's share
// of the device (offset >= the cursor it was read at), plus the
// cross-window overlap this registry requires (the widest signature span
// minus one byte, so a magic straddling a window boundary is always whole
// in the next window read). The matches live in the caller's buffer.
struct WindowMatches {
	std::size_t bytesRead = 0;
	std::size_t overlap = 0;
	std::span<const Match> matches;
};

// Fills `buffer` from the device at `offset`; a short read shrinks the
// returned span (end of device), a fault propagates as the typed error it
// is. Shared by the window scan and by a single carve attempt.
Result<std::span<const std::byte>>
readWindow(BlockDevice& device, std::uint64_t offset, std::span<std::byte> buffer);

// What one window read needs: where to read, what to read into, and where to
// put the matches. Bundled so no caller can pair a cursor with another
// window's buffer.
struct WindowRead {
	std::uint64_t cursor = 0;
	std::span<std::byte> window;
	std::vector<Match>* matches = nullptr; // non-owning, never null
};

// Reads one window at `read.cursor` and returns its matches, dropping any hit
// whose computed offset falls before that cursor (already resolved, or
// skipped, while processing the previous, overlapping window).
Result<WindowMatches>
readAndMatch(BlockDevice& device, const WindowRead& read, const CarverRegistry& registry);

} // namespace revenant::carve
