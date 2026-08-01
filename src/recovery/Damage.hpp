// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Which of an artifact's bytes a run had to invent, and nothing else.
// Not a public interface: its one caller is the delivery step, and the manifest
// carries the answer rather than the question.

#include <cstdint>
#include <span>
#include <vector>

#include "revenant/core/io/BadRange.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::recovery {

// Which of an artifact's bytes the device would not give up: the parts of
// `extents` that fall inside the run's `damage` map, stated in the device's own
// coordinates — the numbers an operator can check against any other tool.
//
// `startBytes` is where the run's zero sits on the device. A run scoped to a
// partition records extents relative to its window while the map is
// device-absolute, so the two only line up once the window's offset is added;
// for a whole-source run it is zero and nothing moves.
//
// An artifact with no extents — resident content, which was read out of a
// record rather than off the device — can never be marked, and neither can one
// whose extents miss every fault. Both answer empty, which is what "these are
// the device's bytes" looks like.
[[nodiscard]] std::vector<BadRange> inventedIn(
	std::span<const fs::Extent> extents,
	std::span<const BadRange> damage,
	std::uint64_t startBytes);

} // namespace revenant::recovery
