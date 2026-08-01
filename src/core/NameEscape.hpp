// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The lossless escape ADR-0010 mandates for on-disk name content
// that must not pass through as itself. The spelling travels with the
// transforms that emit it, so a GPT partition label and an NTFS filename
// report the same escape for the same byte; not a public interface.

#include <cstddef>
#include <cstdint>
#include <string>

namespace revenant {

// The byte an escape opens with. It is spelled here and nowhere else: a decoder
// that hands bytes on unescaped has to reserve this one, or its output cannot be
// told from an escape it did not emit — so the reservation reads the sigil from
// here rather than repeating it (`src/fs/PathSafeByte.cpp`).
inline constexpr char kEscapeSigil = '%';

// `%XX`, uppercase hex — one byte that cannot be handed on as itself.
void appendEscapedByte(std::string& out, std::byte raw);

// `%uXXXX`, uppercase hex — one UTF-16 code unit that will not decode.
void appendEscapedCodeUnit(std::string& out, std::uint16_t unit);

} // namespace revenant
