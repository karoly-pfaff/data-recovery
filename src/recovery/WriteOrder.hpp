// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The order winners are written in, which is not the order they are
// named in. Not a public interface.

#include <cstdint>
#include <span>
#include <vector>

#include "revenant/recovery/Candidate.hpp"

namespace revenant::recovery {

// One winner and the number its output name is built from — its place in
// device order, which the write order deliberately does not follow.
struct Ordered {
	const Candidate* winner; // non-owning; borrowed from the caller's winners
	std::uint64_t ordinal;
};

// Named artifacts first, then carved ones, each group keeping device order.
//
// De-duplication has to be able to say "the named one wins", and writing the
// named artifacts first is what guarantees a carved duplicate always arrives
// second in a single pass. Ordinals still come from device order, so the names
// on disk are unaffected: two runs over one device produce the same output.
[[nodiscard]] std::vector<Ordered> orderedForWriting(std::span<const Candidate> winners);

} // namespace revenant::recovery
