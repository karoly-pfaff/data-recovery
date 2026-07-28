// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/Interrupt.hpp"

#include <gtest/gtest.h>

#include <csignal>

namespace {

using revenant::cli::catchInterrupts;
using revenant::cli::interrupted;

// Ctrl-C only raises a flag; the run reads it between chunks and stops
// cleanly, which is what keeps an interrupted session resumable.
TEST(Interrupt, NoticesAnInterrupt) {
	catchInterrupts();
	ASSERT_FALSE(interrupted());
	static_cast<void>(std::raise(SIGINT));
	EXPECT_TRUE(interrupted());
}

// Installing the handler starts a fresh watch: what stopped a previous run
// must not stop the next one.
TEST(Interrupt, StartsAFreshWatchEachTime) {
	catchInterrupts();
	static_cast<void>(std::raise(SIGINT));
	ASSERT_TRUE(interrupted());
	catchInterrupts();
	EXPECT_FALSE(interrupted());
}

} // namespace
