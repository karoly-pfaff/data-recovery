// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The one line that lets a person recognize a partition as theirs.
// Deliberately a convenience: the well-known types are named and everything else
// falls back to the raw type rather than growing a registry of every vendor code
// that has ever been issued. Offset and size disambiguate the rest.

#include <cstdint>
#include <string>

#include "revenant/volume/GptPartitions.hpp"

namespace revenant::volume {

// A name for a well-known MBR type byte, or `type 0xNN` for anything else.
[[nodiscard]] std::string labelOfMbrType(std::uint8_t typeCode);

// A GPT partition's own name when it has one; otherwise a name for a well-known
// type GUID, and `GPT partition` for anything else.
[[nodiscard]] std::string labelOfGptPartition(const GptPartition& partition);

} // namespace revenant::volume
