// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Which decoder a volume's names need is filesystem knowledge and lives here;
// running a decoder is not, and the UTF-16LE transform every filesystem and
// `volume/` alike reaches for lives at `revenant/core/Utf16Name.hpp`.

#include <cstddef>
#include <span>

#include "revenant/core/Utf16Name.hpp"

namespace revenant::fs {

// Decodes an on-disk name that is *raw bytes with no enforced encoding* — ext4's
// case (ADR-0010). Well-formed UTF-8 passes through unchanged, which is what a
// Linux volume almost always holds, so this is a validation rather than a
// transcoding. Anything else is escaped losslessly as `%XX` (uppercase hex) one
// byte at a time: an invalid, truncated or overlong sequence, a surrogate, a
// code point past U+10FFFF, a NUL or control byte, and the two characters that
// may never pass through as themselves — `/`, which would split a
// volume-relative path, and `%`, which would make an escape ambiguous.
// `lossless` is false whenever any escape fires. Never throws; always produces
// valid UTF-8 (fuzz-tested invariant).
[[nodiscard]] DecodedName decodeRawName(std::span<const std::byte> raw);

} // namespace revenant::fs
