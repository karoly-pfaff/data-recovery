// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <vector>

namespace mft_record_test_support {

constexpr std::size_t kRecordSize = 1024;

[[nodiscard]] std::vector<std::byte> makeValidRecord();

} // namespace mft_record_test_support
