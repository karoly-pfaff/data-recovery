// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. ADR-0010's rule about which bytes may reach a recovered path, asked
// of decoders that hand on bytes carrying no encoding promise: ext4's raw names
// and FAT's OEM short names. Not a public interface.

#include <cstddef>

namespace revenant::fs {

// Whether one byte may be handed on as itself: printable ASCII, minus `/`, which
// would split a volume-relative path, and the escape sigil, which would make an
// escape ambiguous. Its two callers — `decodeRawName` and `decodeShortName` — are
// the decoders whose input carries no encoding promise; a decoder that knows its
// input is UTF-16 never asks, because every byte it emits it wrote itself.
[[nodiscard]] bool passesThroughAsItself(std::byte raw) noexcept;

} // namespace revenant::fs
