// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to JpegCarver's marker walk — NOT a public interface. Consumed
// only by JpegMarkerWalk.cpp in this directory. Subject to change without
// notice from outside src/carve/formats/.

#include <cstdint>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

// Where an entropy scan ended: a genuine terminator was found at `pos`
// (pos left on its 0xFF prefix), or the input was exhausted before one
// could be confirmed (pos is the last position known to hold real,
// already-classified entropy bytes). Exhaustion is an expected value on
// truncated input, not an error — "errors are values" (AGENTS.md).
struct EntropyOutcome {
	std::uint64_t pos;
	bool foundTerminator;
};

// Scans entropy-coded data from `pos` (0xFF 0x00 byte-stuffing, RST
// continuations) and returns how it ended: a terminator found, or the
// input exhausted first.
Result<EntropyOutcome> scanEntropyData(ByteReader& reader, std::uint64_t pos);

} // namespace revenant::carve
