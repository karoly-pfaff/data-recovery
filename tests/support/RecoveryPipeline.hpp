// SPDX-License-Identifier: GPL-3.0-or-later
// The whole tool, once: mount, recover, index, arbitrate, extract — over a
// generated NTFS image. Integration tests that need a complete run derive from
// this and assert whatever they came for.
//
// Not yet the only copy: ArbitratedRecoveryTest assembles an equivalent run for
// its own seam, which is arbitration rather than extraction. Folding it in wants
// a second entry point and is a change of its own, not a docs story's.
#pragma once

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/io/ImageFileDevice.hpp"
#include "revenant/recovery/Candidate.hpp"
#include "revenant/recovery/IndexingVisitors.hpp"
#include "revenant/recovery/RecoverySink.hpp"
#include "support/TempDir.hpp"
#include "support/TempFile.hpp"

namespace revenant::testing {

class RecoveryPipeline : public ::testing::Test {
protected:
	RecoveryPipeline();

	// Runs the full hybrid recovery. Deliberately not called from the
	// constructor: a test that measures the source before and after needs to
	// choose the moment.
	void runFullRecovery();

	[[nodiscard]] const TempFile& image() const noexcept {
		return image_;
	}

	[[nodiscard]] std::filesystem::path recovered(std::string_view relative) const;

	[[nodiscard]] const revenant::recovery::ExtractionStats& stats() const noexcept {
		return stats_;
	}

private:
	void discover();
	void runHybrid(
		revenant::recovery::IndexingEntryVisitor& entries,
		revenant::recovery::IndexingCandidateVisitor& candidates);
	void extractWinners();
	void writeWinners(const std::vector<revenant::recovery::Candidate>& winners);

	TempFile image_;
	std::unique_ptr<revenant::ImageFileDevice> device_;
	revenant::carve::CarverRegistry registry_;
	revenant::carve::SignatureScanner scanner_;
	TempDir session_;
	TempDir output_;
	revenant::recovery::ExtractionStats
		stats_{.filesWritten = 0, .bytesWritten = 0, .failed = 0, .renamed = 0, .deduplicated = 0};
};

} // namespace revenant::testing
