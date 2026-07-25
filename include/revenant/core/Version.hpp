// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace revenant {

// The librevenant semantic version, injected by the build from the CMake
// project version so the two can never drift.
[[nodiscard]] std::string_view version() noexcept;

} // namespace revenant
