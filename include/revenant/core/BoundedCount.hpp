// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant {

// ADR-0009 bounded allocation: no size or count read from untrusted, on-disk
// data may size an allocation or loop directly. `boundedCount` is the single
// checkpoint every such value passes through before it drives one — `bound`
// is always a caller-owned, documented constant (e.g. "remaining device
// bytes", "max attribute count"), never itself derived from `untrusted`.
// Comparison happens in std::uint64_t space so a `T` wider than
// std::size_t on paper (e.g. a 64-bit on-disk field read on a platform
// with a 32-bit std::size_t) can never wrap the comparison silently.
template <std::unsigned_integral T>
[[nodiscard]] Result<std::size_t> boundedCount(T untrusted, std::size_t bound) noexcept {
	if (static_cast<std::uint64_t>(untrusted) > static_cast<std::uint64_t>(bound)) {
		return Error{.code = ErrorCode::kOutOfRange};
	}
	return static_cast<std::size_t>(untrusted);
}

} // namespace revenant
