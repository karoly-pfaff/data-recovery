// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Turning "everything the filesystem did not account for" into the
// list of regions a resumable, checkpointed scan actually walks. Not a public
// interface.

#include <cstdint>
#include <span>
#include <vector>

#include "revenant/carve/SignatureScanner.hpp"

namespace revenant::recovery {

// The part of `regions` at or after `cursor`, with the region straddling it
// cut short. This is what a resume cursor means: everything behind it has been
// searched already.
[[nodiscard]] std::vector<carve::ScanRegion>
regionsFrom(std::span<const carve::ScanRegion> regions, std::uint64_t cursor);

// The same regions, none longer than `chunkBytes`.
//
// A region bounds where a signature is *looked for*, never how long a file may
// be, and the scanner already reads a signature's reach past a region's end so
// a magic straddling the boundary is still found — so cutting one in two costs
// nothing. It is what makes the checkpoint interval bounded on a device with a
// single enormous gap, which is exactly what carve-only over a formatted disk
// produces.
[[nodiscard]] std::vector<carve::ScanRegion>
chunked(std::span<const carve::ScanRegion> regions, std::uint64_t chunkBytes);

} // namespace revenant::recovery
