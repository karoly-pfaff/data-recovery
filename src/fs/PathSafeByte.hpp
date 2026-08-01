// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. ADR-0010's rule about which bytes may reach a recovered path, asked
// of decoders that hand on bytes carrying no encoding promise: ext4's raw names
// and FAT's OEM short names. Not a public interface.

#include <cstddef>

namespace revenant::fs {

// Whether one byte may be handed on as itself: printable ASCII, minus `/`,
// which would split a volume-relative path, and `%`, which would make an escape
// ambiguous. Every decoder asks this of a single byte whatever encoding
// produced it, so the answer is spelled once.
[[nodiscard]] bool passesThroughAsItself(std::byte raw) noexcept;

} // namespace revenant::fs
