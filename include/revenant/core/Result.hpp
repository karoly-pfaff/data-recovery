// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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
    template <typename F>
    [[nodiscard]] auto map(F&& transform) const
        -> Result<decltype(transform(std::declval<const T&>()))> {
        if (!hasValue()) {
            return error();
        }
        return std::forward<F>(transform)(value());
    }

private:
    std::variant<T, Error> storage_;
};

} // namespace revenant
