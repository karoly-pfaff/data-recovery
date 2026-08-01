// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/PathSafeByte.hpp"

#include <cstddef>

#include "core/NameEscape.hpp"

namespace revenant::fs {

namespace {

// The byte that separates a volume-relative path, which a name may therefore
// never contain. The escape sigil is the other reserved byte, and it is read
// from where it is emitted rather than repeated here.
constexpr char kPathSeparator = '/';

constexpr unsigned kFirstPrintableAscii = 0x20U;
constexpr unsigned kLastPrintableAscii = 0x7EU;

} // namespace

bool passesThroughAsItself(std::byte raw) noexcept {
	const auto value = std::to_integer<unsigned>(raw);
	return value >= kFirstPrintableAscii && value <= kLastPrintableAscii &&
		   raw != std::byte{kPathSeparator} && raw != std::byte{kEscapeSigil};
}

} // namespace revenant::fs
