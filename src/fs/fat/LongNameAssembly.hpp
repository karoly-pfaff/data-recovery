// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The long-name fragments preceding a short entry, turned back into
// one name. Not a public interface.

#include <cstddef>
#include <optional>
#include <vector>

#include "revenant/core/Utf16Name.hpp"
#include "revenant/fs/fat/DirectoryEntry.hpp"

namespace revenant::fs::fat {

// FAT allows at most 20 fragments (13 code units each, 255 characters plus the
// terminator). More than that is not a long name, it is a directory built to
// exhaust whoever reads it (ADR-0009).
inline constexpr std::size_t kMaxNameFragments = 20;

// Collects the fragments a directory walk passes over and hands back the name
// when the short entry they belong to arrives.
//
// Fragments are stored last-first, immediately before that short entry, so
// physical order is the reverse of name order. Physical order is also all a
// *deleted* set has left: deletion overwrites the ordinal byte of every slot,
// so the numbers that would otherwise order them are gone. The assembler
// therefore reverses what it collected, and uses the ordinals only to check a
// live set against that order.
class LongNameBuilder {
public:
	void add(const LongNameFragment& fragment);

	// The assembled name, or nothing when no fragments were collected, too many
	// were, or a live set's ordinals contradict the order it was stored in —
	// in which case the caller falls back to the short name rather than
	// assembling one out of unrelated slots.
	[[nodiscard]] std::optional<DecodedName> assembled() const;

	void reset();

private:
	std::vector<LongNameFragment> fragments_;
};

} // namespace revenant::fs::fat
