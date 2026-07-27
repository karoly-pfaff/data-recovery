// SPDX-License-Identifier: GPL-3.0-or-later
// Internal. The attribute-header wrapping shared by AttributeBuilder.cpp and
// FileAttributes.cpp. Not a public interface.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace revenant::imagegen::ntfs {

// On-disk attribute type codes.
inline constexpr std::uint32_t kStandardInformationType = 0x10;
inline constexpr std::uint32_t kFileNameType = 0x30;
inline constexpr std::uint32_t kDataType = 0x80;

// Wraps `content` in a resident attribute header of `type`, padding the whole
// attribute to the 8-byte multiple the record walker requires.
[[nodiscard]] std::vector<std::byte>
residentAttribute(std::uint32_t type, std::span<const std::byte> content);

// The UTF-16LE form of an ASCII name, as $FILE_NAME stores it.
[[nodiscard]] std::vector<std::byte> widenAscii(std::span<const char> ascii);

} // namespace revenant::imagegen::ntfs
