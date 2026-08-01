// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/recovery/RunScope.hpp"

namespace revenant::testing {

// The scope a test states when it is not about partitions: this device, all of
// it, walked as whatever its own table says it is.
//
// `HybridRecovery` takes a scope rather than a device because the two must
// arrive together (story-0610), and resolving choice zero is what every run
// that names no partition does. It cannot fail — a source with no readable
// table is one volume, which is an answer — so a test may take the value.
[[nodiscard]] inline recovery::RunScope wholeSourceScope(BlockDevice& device) {
	auto resolved = recovery::RunScope::resolve(device, 0);
	return std::move(resolved.value());
}

} // namespace revenant::testing
