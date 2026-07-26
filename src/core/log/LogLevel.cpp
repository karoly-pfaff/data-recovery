// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/log/LogLevel.hpp"

#include <string_view>

namespace revenant {

std::string_view toString(LogLevel level) noexcept {
	switch (level) {
	case LogLevel::kTrace:
		return "trace";
	case LogLevel::kDebug:
		return "debug";
	case LogLevel::kInfo:
		return "info";
	case LogLevel::kWarn:
		return "warn";
	case LogLevel::kError:
		return "error";
	}
	return "unknown";
}

} // namespace revenant
