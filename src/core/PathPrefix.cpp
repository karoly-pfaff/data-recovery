// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/PathPrefix.hpp"

#include <algorithm>
#include <filesystem>

namespace revenant {

bool startsPath(const std::filesystem::path& ancestor, const std::filesystem::path& path) {
	const auto reach = std::ranges::mismatch(ancestor, path);
	return reach.in1 == ancestor.end();
}

} // namespace revenant
