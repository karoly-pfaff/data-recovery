// SPDX-License-Identifier: GPL-3.0-or-later
// Internal. The ISO base media top-level box walk behind Mp4Carver. Not a
// public interface.
#pragma once

#include <cstdint>

#include "revenant/core/ByteReader.hpp"

namespace revenant::carve {

// Where the box walk got to and which structural boxes it saw. `end` is the
// offset just past the last box that parsed — the exact extent of the file.
struct Mp4WalkOutcome {
	std::uint64_t end = 0;
	bool sawFtyp = false;
	bool sawMoov = false;
	bool sawMdat = false;
};

// Walks the top-level boxes from offset 0. The first box must be `ftyp`;
// anything else yields an empty outcome, because these bytes are not an ISO
// base media file.
[[nodiscard]] Mp4WalkOutcome walkMp4Boxes(const ByteReader& reader);

} // namespace revenant::carve
