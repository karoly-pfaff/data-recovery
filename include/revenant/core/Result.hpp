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
	// Accesses go through get_if, not std::get: the alternative was just
	// checked, so the pointer is always valid (defined, no-throw) — std::get's
	// throwing path would poison noexcept callers (bugprone-exception-escape).
	template <typename F>
	[[nodiscard]] auto map(F&& transform) const
		-> Result<decltype(transform(std::declval<const T&>()))> {
		if (!hasValue()) {
			return *std::get_if<1>(&storage_);
		}
		return std::forward<F>(transform)(*std::get_if<0>(&storage_));
	}

	// Monadic bind: `transform` returns a Result<U>; errors are flattened.
	template <typename F>
	[[nodiscard]] auto andThen(F&& transform) const -> std::invoke_result_t<F, const T&> {
		using U = std::invoke_result_t<F, const T&>;
		if (!hasValue()) {
			return U{*std::get_if<1>(&storage_)};
		}
		return std::forward<F>(transform)(*std::get_if<0>(&storage_));
	}

private:
	std::variant<T, Error> storage_;
};

} // namespace revenant
