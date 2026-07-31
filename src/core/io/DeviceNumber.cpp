// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/io/DeviceNumber.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

#include "core/TextNumber.hpp"

namespace revenant {

std::optional<std::uint64_t> deviceKeyIn(std::string_view text) {
	const auto colon = text.find(':');
	if (colon == std::string_view::npos) {
		return std::nullopt;
	}
	const auto major = numberIn(text.substr(0, colon));
	const auto minor = numberIn(text.substr(colon + 1));
	if (!major.has_value() || !minor.has_value()) {
		return std::nullopt;
	}
	return deviceKey(major.value(), minor.value());
}

} // namespace revenant
