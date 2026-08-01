// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. One answer to "what number does this text hold", for text that came
// from outside the program — a sysfs file, the kernel's mount table.
//
// `std::from_chars`'s [first, last) pointer pair is the only portable overload,
// so the pointer arithmetic it needs — and the suppression that arithmetic
// costs — is written down once here instead of at every call site.

#include <cstdint>
#include <optional>
#include <string_view>

namespace revenant {

// The number at the front of `text` in `base`, or nothing when it starts with
// no number at all. Trailing characters are not an error: a sysfs file's number
// is followed by a newline, and an escape sequence by the rest of its field.
[[nodiscard]] std::optional<std::uint64_t> numberIn(std::string_view text, int base = 10);

} // namespace revenant
