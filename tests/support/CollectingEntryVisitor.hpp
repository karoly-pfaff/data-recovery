// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include "revenant/fs/RecoveredEntry.hpp"

namespace revenant::testing {

// Test double: records every reported entry for assertions.
class CollectingEntryVisitor final : public fs::EntryVisitor {
public:
	void onEntry(const fs::RecoveredEntry& entry) override {
		entries_.push_back(entry);
	}

	[[nodiscard]] const std::vector<fs::RecoveredEntry>& entries() const noexcept {
		return entries_;
	}

private:
	std::vector<fs::RecoveredEntry> entries_;
};

} // namespace revenant::testing
