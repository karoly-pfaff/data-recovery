// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SafeArith.hpp"

#include <cstdint>
#include <limits>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant {

// In each of these the first two parameters are the operands and the third is a
// diagnostic offset. They are always passed by name at call sites, so the swap
// risk the check warns about does not apply.

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Result<std::uint32_t> safeMul32(std::uint32_t a, std::uint32_t b, std::uint64_t offset) noexcept {
	const auto product = static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b);
	if (product > std::numeric_limits<std::uint32_t>::max()) {
		return Error{.code = ErrorCode::kOverflow, .offset = offset};
	}
	return static_cast<std::uint32_t>(product);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Result<std::uint64_t> safeMul64(std::uint64_t a, std::uint64_t b, std::uint64_t offset) noexcept {
	if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) {
		return Error{.code = ErrorCode::kOverflow, .offset = offset};
	}
	return a * b;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Result<std::uint64_t> safeAdd64(std::uint64_t a, std::uint64_t b, std::uint64_t offset) noexcept {
	if (b > std::numeric_limits<std::uint64_t>::max() - a) {
		return Error{.code = ErrorCode::kOverflow, .offset = offset};
	}
	return a + b;
}

} // namespace revenant
