// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/Version.hpp"

#include <string_view>

namespace revenant {

std::string_view version() noexcept {
	return REVENANT_VERSION;
}

} // namespace revenant
