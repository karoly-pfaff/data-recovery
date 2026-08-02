// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Which of a run's recovered bytes the device would not give up, and
// where those bytes sit on the disk. Split from the delivery that calls it
// because marking damage and sequencing a run are two jobs — and because this
// is where the run's two coordinate systems are reconciled, which is worth
// finding in one place. Not a public interface.

#include <cstdint>
#include <vector>

#include "cli/RunDelivery.hpp"
#include "revenant/recovery/ArtifactRecord.hpp"
#include "revenant/recovery/RecoverySink.hpp"

namespace revenant::cli {

// Which of each artifact's bytes the run had to invent, written onto the
// records that are about to become the manifest.
//
// It happens once, where the finished extraction and the stack that did the
// reading meet. A preview is marked too: the overlap is a fact about extents,
// not about whether anything was written.
[[nodiscard]] recovery::Extraction
marked(recovery::Extraction extraction, const DeliverySource& source);

// Every artifact restated on the device the operator handed over.
//
// A scoped run records extents relative to its window, while the bad-sector map
// is device-absolute — and a document whose two range fields count from
// different origins is one an operator cannot compare against anything, least
// of all against itself. The manifest is one coordinate system, and it is the
// disk's. For a whole-source run the offset is zero and nothing moves.
[[nodiscard]] std::vector<recovery::ArtifactRecord>
onTheDevice(std::vector<recovery::ArtifactRecord> artifacts, std::uint64_t startBytes);

} // namespace revenant::cli
