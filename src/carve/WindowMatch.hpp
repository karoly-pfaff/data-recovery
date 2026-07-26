// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to SignatureScanner's window loop — NOT a public interface. Not
// part of story-0014's produced Interfaces block; consumed only by
// SignatureScanner.cpp and WindowMatch.cpp in this directory. Subject to
// change without notice from outside src/carve/.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/FormatCarver.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::carve {

// A signature hit inside one window, in device coordinates.
struct Match {
	std::uint64_t offset;
	const FormatCarver* carver;
};

// Matches found in one window read, already filtered to that window's share
// of the device (offset >= the cursor it was read at), plus the
// cross-window overlap this registry requires (the widest signature span
// minus one byte, so a magic straddling a window boundary is always whole
// in the next window read).
struct WindowMatches {
	std::size_t bytesRead;
	std::size_t overlap;
	std::vector<Match> matches;
};

// Fills `buffer` from the device at `offset`; a short read shrinks the
// returned span (end of device), a fault propagates as the typed error it
// is. Shared by the window scan and by a single carve attempt.
Result<std::span<const std::byte>>
readWindow(BlockDevice& device, std::uint64_t offset, std::span<std::byte> buffer);

// Reads one window at `cursor` and returns its matches, dropping any hit
// whose computed offset falls before `cursor` (already resolved, or
// skipped, while processing the previous, overlapping window).
Result<WindowMatches> readAndMatch(
	BlockDevice& device,
	std::uint64_t cursor,
	std::span<std::byte> windowBuffer,
	const CarverRegistry& registry);

} // namespace revenant::carve
