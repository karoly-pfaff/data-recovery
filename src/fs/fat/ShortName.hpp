// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The 8.3 name in a FAT directory entry, decoded to UTF-8. Not a
// public interface: callers see the decoded name on a parsed entry.

#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Utf16Name.hpp"

namespace revenant::fs::fat {

// The character a deleted entry's name begins with. `0xE5` overwrote the
// original and nothing on the volume holds it, so the name is marked
// approximate rather than guessed at.
inline constexpr char kLostFirstCharacter = '_';

// Decodes the 11 raw name bytes of a short entry.
//
// The name is in the volume's OEM code page, which the volume does not record,
// so guessing one would silently mistranslate every non-English name. Bytes
// 0x20-0x7E decode as themselves; everything else — and `/` and `%`, which
// carry structural meaning in a path — is escaped `%XX`, the same lossless
// convention ADR-0010 applies to undecodable UTF-16. `lossless` comes back
// false whenever an escape fires or the first character was lost.
//
// `caseFlags` is the entry's `NTRes` byte: bit 3 lower-cases the base name and
// bit 4 the extension. `deleted` says the first byte is the `0xE5` marker
// rather than a character.
[[nodiscard]] DecodedName
decodeShortName(std::span<const std::byte> raw, std::uint8_t caseFlags, bool deleted);

} // namespace revenant::fs::fat
