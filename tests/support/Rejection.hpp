// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// How a byte parser said no, as one comparable value.
//
// A rejection is a code *and* the byte offset that caused it, and every
// filesystem's parser tests assert on both together — one field at a time would
// pass a test that names the wrong field. Written once here rather than once per
// filesystem; the duplication gate is what found the copies.

#include <cstdint>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::testing {

struct Rejection {
	ErrorCode code{};
	std::uint64_t offset{};

	friend bool operator==(const Rejection&, const Rejection&) = default;
};

// Why `parsed` failed. A parse that unexpectedly *succeeded* reports kNotFound —
// which none of these parsers produces — so a passing parse fails the comparison
// rather than slipping through it.
template <typename T> [[nodiscard]] Rejection rejectionOf(const Result<T>& parsed) {
	if (parsed.hasValue()) {
		return Rejection{.code = ErrorCode::kNotFound, .offset = 0};
	}
	return Rejection{.code = parsed.error().code, .offset = parsed.error().offset};
}

[[nodiscard]] inline Rejection invalidAt(std::uint64_t offset) {
	return Rejection{.code = ErrorCode::kInvalidArgument, .offset = offset};
}

[[nodiscard]] inline Rejection outOfRangeAt(std::uint64_t offset) {
	return Rejection{.code = ErrorCode::kOutOfRange, .offset = offset};
}

} // namespace revenant::testing
