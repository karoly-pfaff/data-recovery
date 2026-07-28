// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "revenant/recovery/HybridRecovery.hpp"

namespace revenant::testing {

// A progress reporter that remembers every cursor it was told about and stops
// the scan after `stopAfter` of them. Zero never stops, which is what the tests
// that are about recovery rather than about resumability want.
class RecordingProgress final : public recovery::ScanProgress {
public:
	RecordingProgress() = default;

	explicit RecordingProgress(std::size_t stopAfter) noexcept : stopAfter_(stopAfter) {}

	bool onScanned(std::uint64_t cursor) override {
		cursors_.push_back(cursor);
		return stopAfter_ == 0 || cursors_.size() < stopAfter_;
	}

	[[nodiscard]] const std::vector<std::uint64_t>& cursors() const noexcept {
		return cursors_;
	}

private:
	std::vector<std::uint64_t> cursors_;
	std::size_t stopAfter_ = 0;
};

} // namespace revenant::testing
