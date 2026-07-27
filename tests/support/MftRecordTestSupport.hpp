// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <vector>

namespace mft_record_test_support {

constexpr std::size_t kRecordSize = 1024;

// Attribute offsets inside the record built by makeValidRecord(). Kept beside
// the builder so a layout change updates both; tests address attributes by
// name rather than by a bare hex literal.
constexpr std::size_t kStandardInfoAttributeOffset = 0x38;
constexpr std::size_t kFileNameAttributeOffset = 0x80;
constexpr std::size_t kDataAttributeOffset = 0xF0;

[[nodiscard]] std::vector<std::byte> makeValidRecord();

} // namespace mft_record_test_support
