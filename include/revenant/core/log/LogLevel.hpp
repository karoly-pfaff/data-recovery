// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace revenant {

// Severity ordering is the enumerator ordering; filtering relies on it.
enum class LogLevel : std::uint8_t { kTrace, kDebug, kInfo, kWarn, kError };

[[nodiscard]] std::string_view toString(LogLevel level) noexcept;

} // namespace revenant
