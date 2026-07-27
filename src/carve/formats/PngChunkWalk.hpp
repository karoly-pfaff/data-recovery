// SPDX-License-Identifier: GPL-3.0-or-later
// Internal. The PNG chunk-list walk behind PngCarver: signature, then
// length/type/data/CRC chunks through IEND. Not a public interface.
#pragma once

#include <cstdint>

#include "revenant/core/ByteReader.hpp"

namespace revenant::carve {

// Where the walk got to and what it saw on the way. `end` is the offset just
// past the last chunk whose CRC verified — the exact extent when IEND was
// reached, and the trustworthy prefix when it was not.
struct PngWalkOutcome {
	std::uint64_t end = 0;
	bool sawIhdr = false;
	bool reachedIend = false;
};

// Walks the chunk list from the start of `reader`, which the caller has
// already confirmed begins with the PNG signature.
[[nodiscard]] PngWalkOutcome walkPngChunks(const ByteReader& reader);

} // namespace revenant::carve
