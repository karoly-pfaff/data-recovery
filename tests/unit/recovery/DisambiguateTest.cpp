// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/Disambiguate.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace {

using revenant::recovery::disambiguate;
using revenant::recovery::kMaxDisambiguationAttempts;

TEST(Disambiguate, FreeNameIsReturnedUnchanged) {
	const auto taken = [](std::string_view) { return false; };
	EXPECT_EQ(disambiguate("report.txt", taken), "report.txt");
}

TEST(Disambiguate, SingleCollisionInsertsBeforeLastExtensionDot) {
	const auto taken = [](std::string_view name) { return name == "report.tar.gz"; };
	EXPECT_EQ(disambiguate("report.tar.gz", taken), "report.tar (2).gz");
}

TEST(Disambiguate, TwoCollisionsAdvanceToNextNumber) {
	const auto taken = [](std::string_view name) {
		return name == "report.txt" || name == "report (2).txt";
	};
	EXPECT_EQ(disambiguate("report.txt", taken), "report (3).txt");
}

TEST(Disambiguate, ExtensionlessNameAppendsSuffix) {
	const auto taken = [](std::string_view name) { return name == "README"; };
	EXPECT_EQ(disambiguate("README", taken), "README (2)");
}

TEST(Disambiguate, LastAttemptAtBoundSucceeds) {
	// `taken` reports every name free EXCEPT the original and the first
	// (kMaxDisambiguationAttempts - 1) numbered candidates - the free slot
	// falls exactly on the kMaxDisambiguationAttempts-th numbered attempt,
	// pinning that the loop covers the full bound with no off-by-one.
	int numberedCalls = 0;
	const auto taken = [&numberedCalls](std::string_view candidate) {
		if (candidate == "name.ext") {
			return true;
		}
		++numberedCalls;
		return numberedCalls < kMaxDisambiguationAttempts;
	};
	const auto result = disambiguate("name.ext", taken);
	EXPECT_EQ(result, "name (" + std::to_string(kMaxDisambiguationAttempts + 1) + ").ext");
}

TEST(Disambiguate, BeyondBoundReturnsOverflowFallback) {
	const auto taken = [](std::string_view) { return true; };
	const auto result = disambiguate("name.ext", taken);
	EXPECT_EQ(result, "name.ext (overflow)" + std::to_string(kMaxDisambiguationAttempts));
}

} // namespace
