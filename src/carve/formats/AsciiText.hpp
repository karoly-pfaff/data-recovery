// SPDX-License-Identifier: GPL-3.0-or-later
// Internal. Reading raw bytes as ASCII text, for the carvers that name a file
// by a string stored inside it. Not a public interface.
#pragma once

#include <cstddef>
#include <span>
#include <string>

namespace revenant::carve {

// The bytes as text, one char per byte. Nothing is validated or transcoded:
// callers compare against known ASCII markers (`NIKON`, `word/`), so a byte
// that is not ASCII simply fails to match, which is the correct answer.
[[nodiscard]] std::string asciiText(std::span<const std::byte> raw);

} // namespace revenant::carve
