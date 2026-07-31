// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the format carvers: the one answer to "do these bytes open with
// this signature?". NOT a public interface; consumed only from
// src/carve/formats/.

#include <cstddef>
#include <span>

#include "revenant/core/ByteReader.hpp"

namespace revenant::carve {

// True when the reader's opening bytes are exactly `signature`. A reader too
// short to hold one is not a match rather than an error: carving is attempted
// at every candidate offset and most offsets are not files.
[[nodiscard]] bool headMatches(const ByteReader& reader, std::span<const std::byte> signature);

} // namespace revenant::carve
