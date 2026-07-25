// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/Result.hpp"

#include <gtest/gtest.h>

#include <variant>

#include "revenant/core/Error.hpp"

namespace {

using revenant::Error;
using revenant::ErrorCode;
using revenant::Result;

TEST(Result, HoldsValue) {
    const Result<int> result{42};
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(result.value(), 42);
}

TEST(Result, HoldsError) {
    const Result<int> result{Error{.code = ErrorCode::kOutOfRange, .offset = 7}};
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error(), (Error{ErrorCode::kOutOfRange, 7}));
}

TEST(Result, ValueOnErrorIsDefinedTestableFailure) {
    const Result<int> result{Error{.code = ErrorCode::kIoFailure}};
    EXPECT_THROW(static_cast<void>(result.value()), std::bad_variant_access);
}

TEST(Result, ErrorOnValueIsDefinedTestableFailure) {
    const Result<int> result{1};
    EXPECT_THROW(static_cast<void>(result.error()), std::bad_variant_access);
}

TEST(Result, MapTransformsValue) {
    const Result<int> half{21};
    EXPECT_EQ(half.map([](int v) { return v * 2; }).value(), 42);
}

TEST(Result, MapForwardsError) {
    const Result<int> failed{Error{.code = ErrorCode::kOverflow}};
    EXPECT_EQ(failed.map([](int v) { return v * 2; }).error().code, ErrorCode::kOverflow);
}

} // namespace
