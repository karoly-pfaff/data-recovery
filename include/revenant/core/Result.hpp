// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <type_traits>
#include <utility>
#include <variant>

#include "revenant/core/Error.hpp"

namespace revenant {

// Value-or-typed-Error ("errors are values", AGENTS.md §3). Querying the wrong
// alternative throws std::bad_variant_access — defined and testable, never UB.
// Implicit construction from T and Error is deliberate: it keeps call sites
// (`return Error{…}` / `return computed;`) free of ceremony.
template <typename T> class Result {
public:
	Result(T value) : storage_(std::move(value)) {} // NOLINT(google-explicit-constructor)

	Result(Error error) : storage_(error) {} // NOLINT(google-explicit-constructor)

	[[nodiscard]] bool hasValue() const noexcept {
		return storage_.index() == 0;
	}

	explicit operator bool() const noexcept {
		return hasValue();
	}

	[[nodiscard]] T& value() {
		return std::get<0>(storage_);
	}

	[[nodiscard]] const T& value() const {
		return std::get<0>(storage_);
	}

	[[nodiscard]] const Error& error() const {
		return std::get<1>(storage_);
	}

	// Applies `transform` to the value; forwards the error unchanged.
	//
	// Both alternatives are reached by pointer, and each dereference is
	// guarded by the pointer that produced it. Two gates pull in opposite
	// directions here and this is the shape that satisfies both. Branching on
	// `hasValue()` and then dereferencing `get_if` leaves the compiler looking
	// at an unguarded dereference of a function declared to be able to return
	// nullptr, which GCC reports as `-Wnull-dereference` once the optimizer
	// inlines it — a diagnostic only an optimized GCC build ever sees.
	// Reaching for `std::get` instead brings a throwing path into functions
	// several `noexcept` readers chain through, which clang-tidy reports as
	// `bugprone-exception-escape`. Asking by pointer answers both.
	template <typename F>
	[[nodiscard]] auto map(F&& transform) const
		-> Result<decltype(transform(std::declval<const T&>()))> {
		const T* held = std::get_if<0>(&storage_);
		if (held == nullptr) {
			return heldError();
		}
		return std::forward<F>(transform)(*held);
	}

	// Monadic bind: `transform` returns a Result<U>; errors are flattened.
	template <typename F>
	[[nodiscard]] auto andThen(F&& transform) const -> std::invoke_result_t<F, const T&> {
		using U = std::invoke_result_t<F, const T&>;
		const T* held = std::get_if<0>(&storage_);
		if (held == nullptr) {
			return U{heldError()};
		}
		return std::forward<F>(transform)(*held);
	}

private:
	// The error this holds, without a throwing path. A `std::variant` holds
	// neither alternative only after an assignment threw, which is why
	// `get_if` is allowed to answer nullptr at all; a `Result` in that state
	// is a corrupted object, and the honest thing to hand a caller is an
	// error rather than a value it never produced.
	[[nodiscard]] const Error& heldError() const noexcept {
		static constexpr Error kNeitherAlternative{.code = ErrorCode::kInvalidArgument};
		const Error* failure = std::get_if<1>(&storage_);
		return failure != nullptr ? *failure : kNeitherAlternative;
	}

	std::variant<T, Error> storage_;
};

} // namespace revenant
