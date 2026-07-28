// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "revenant/recovery/Candidate.hpp"

namespace revenant::recovery {

// Where carved files go, and what a carved file with no format of its own is
// called. A carved recovery has no name to keep, so it gets a bucket and a
// number instead of a guess.
inline constexpr std::string_view kCarvedRoot = "carved";
inline constexpr std::string_view kUnknownExtension = "bin";

// The destination-relative name a winner should be written under: a filesystem
// entry keeps the path it had inside the volume, a carved one becomes
// `carved/<ext>/f<ordinal>.<ext>`. `ordinal` is the winner's position in
// device order, so two runs over one device produce the same names.
//
// This is a *proposal*, not a path: it still has to survive
// `sanitizeOutputPath` before anything may be written to it (ADR-0009).
[[nodiscard]] std::string outputNameFor(const Candidate& candidate, std::uint64_t ordinal);

} // namespace revenant::recovery
