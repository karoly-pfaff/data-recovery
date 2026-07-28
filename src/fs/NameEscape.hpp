// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The lossless escape ADR-0010 mandates for on-disk name content
// that must not pass through as itself. Shared by every filesystem's name
// decoder so one convention is spelled once; not a public interface.

#include <cstddef>
#include <cstdint>
#include <string>

namespace revenant::fs {

// `%XX`, uppercase hex — one byte that cannot be handed on as itself.
void appendEscapedByte(std::string& out, std::byte raw);

// `%uXXXX`, uppercase hex — one UTF-16 code unit that will not decode.
void appendEscapedCodeUnit(std::string& out, std::uint16_t unit);

} // namespace revenant::fs
