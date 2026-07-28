// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/Version.hpp"

#include <gtest/gtest.h>

namespace {

TEST(Version, ReportsProjectVersion) {
	EXPECT_EQ(revenant::version(), "0.1.0");
}

} // namespace
