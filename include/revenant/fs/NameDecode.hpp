// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <string>

namespace revenant::fs {

// Result of decoding an on-disk UTF-16LE name (ADR-0010): canonical UTF-8,
// with a flag recording whether every code unit decoded losslessly.
struct DecodedName {
	std::string utf8;
	bool lossless = true;
};

// Decodes UTF-16LE code units (NTFS on-disk names) into canonical UTF-8.
// Surrogate pairs (high D800-DBFF, low DC00-DFFF) become one 4-byte UTF-8
// code point; every other BMP unit becomes 1/2/3 UTF-8 bytes by value.
// Content that cannot be decoded — an unpaired/reversed surrogate, or a
// literal NUL unit (never allowed to pass through as a raw NUL byte) — is
// escaped losslessly as `%uXXXX` (uppercase hex, 4 digits) rather than
// dropped or substituted; a dangling odd trailing byte becomes `%XX`.
// `lossless` is false whenever any escape fires. Never throws; always
// produces valid UTF-8 (fuzz-tested invariant).
[[nodiscard]] DecodedName decodeUtf16Name(std::span<const std::byte> utf16le);

} // namespace revenant::fs
