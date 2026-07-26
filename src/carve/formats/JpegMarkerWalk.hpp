// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to JpegCarver's marker walk — NOT a public interface. Consumed
// only by JpegCarver.cpp in this directory. Subject to change without
// notice from outside src/carve/formats/.

#include <cstdint>

#include "revenant/core/ByteReader.hpp"

namespace revenant::carve {

// The exact extent plus SOS/EOI bookkeeping produced by walking a JPEG's
// marker structure from just past SOI. `reachedEoi=false` covers both a
// structural break and running out of bytes — both are verdicts computed
// from this outcome, never thrown or otherwise surfaced as an error.
struct JpegWalkOutcome {
	std::uint64_t end = 0;
	bool sawSos = false;
	bool reachedEoi = false;
};

// Walks markers from just past SOI. Every read goes through ByteReader's
// bounds-checked accessors; position strictly increases every iteration
// that continues, so the walk always terminates (see story-0010's totality
// argument).
JpegWalkOutcome walkJpegMarkers(ByteReader& reader);

} // namespace revenant::carve
