// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. A fixed-size on-disk record, length-checked once so that the reads
// inside it need no ceremony. FAT and exFAT lay their directories out as arrays
// of 32-byte slots, ext4 its inode tables and group descriptors likewise, so the
// checking is written here once rather than once per filesystem.
//
// Every function is `inline` or a template, so this header is safely includable
// from any translation unit. Not a public interface.

#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::fs {

// A reader bounded to exactly one slot, or kOutOfRange when `slot` is shorter
// than one. Everything below depends on this having been called first.
[[nodiscard]] inline Result<ByteReader>
slotReader(std::span<const std::byte> slot, std::size_t slotBytes) {
	if (slot.size() < slotBytes) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = slot.size()};
	}
	return ByteReader{slot.first(slotBytes)};
}

// A read at a fixed offset inside a slot already sized to a whole entry cannot
// fail, so these unwrap to a value rather than threading a Result nobody can
// act on. `slotReader`'s length check is what makes that true.
[[nodiscard]] inline std::uint8_t slotByteAt(const ByteReader& reader, std::size_t offset) {
	const auto raw = reader.bytes(offset, 1);
	return raw.hasValue() ? std::to_integer<std::uint8_t>(raw.value().front()) : 0U;
}

template <typename T> [[nodiscard]] T slotFieldAt(const ByteReader& reader, std::size_t offset) {
	const auto raw = reader.readLe<T>(offset);
	return raw.hasValue() ? raw.value() : T{0};
}

// The same, big-endian. ext4's journal kept the byte order jbd was written
// with, whichever way the filesystem around it stores its own fields.
template <typename T> [[nodiscard]] T slotFieldBeAt(const ByteReader& reader, std::size_t offset) {
	const auto raw = reader.readBe<T>(offset);
	return raw.hasValue() ? raw.value() : T{0};
}

} // namespace revenant::fs
