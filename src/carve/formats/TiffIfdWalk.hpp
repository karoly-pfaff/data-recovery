// SPDX-License-Identifier: GPL-3.0-or-later
// Internal. The TIFF IFD-chain walk behind the RAW carver. Not a public
// interface.
#pragma once

#include <cstdint>

#include "TiffEntry.hpp"

namespace revenant::carve {

// What the IFD chain accounts for. `end` is the highest offset anything in the
// file points at — the IFD tables, out-of-line entry values, and the image
// data — which is the file's exact extent.
struct TiffWalkOutcome {
	std::uint64_t end = 0;
	bool sawIfd = false;
	bool sawImageData = false;
	bool withinBounds = true;
	bool chainComplete = false; // false when the chain broke or hit the cap
	TiffEntry make;             // the Make tag's entry; tag 0 when absent
};

// Walks the chain from the offset in the TIFF header. The chain is capped: a
// `next` pointer may point backwards, so a crafted file can otherwise loop.
[[nodiscard]] TiffWalkOutcome walkTiffIfds(const TiffContext& tiff);

} // namespace revenant::carve
