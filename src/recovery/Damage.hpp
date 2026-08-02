// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Which of an artifact's bytes a run had to invent, and nothing else.
// Not a public interface: the manifest carries the answer rather than the
// question, so nothing outside this build needs to ask it.
//
// Its one caller is `cli/RunDelivery.cpp`, which is the first include of a
// `src/recovery/` header from another layer's directory. That is downward and
// so allowed, but it is unusual enough to say out loud: the marking has to
// happen where the finished extraction and the run's bad-sector map meet, and
// that place is in `cli/`.

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
