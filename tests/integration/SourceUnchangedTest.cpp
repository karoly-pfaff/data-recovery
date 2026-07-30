// SPDX-License-Identifier: GPL-3.0-or-later
// The guarantee the whole tool rests on, asserted rather than promised: a full
// recovery run must leave the source byte-for-byte as it found it (ADR-0005).
// Everything above the I/O layer is structurally incapable of writing to it —
// BlockDevice declares no write operation — and every open goes through
// openReadOnly. This test is what fails if either of those stops being true.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

#include "imagegen/ntfs/NtfsImageBuilder.hpp"
#include "revenant/carve/BuiltinCarvers.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/Sha256.hpp"
#include "revenant/core/io/ImageFileDevice.hpp"
#include "revenant/recovery/Arbitration.hpp"
#include "revenant/recovery/Candidate.hpp"
#include "revenant/recovery/CandidateIndex.hpp"
#include "revenant/recovery/HybridRecovery.hpp"
#include "revenant/recovery/IndexingVisitors.hpp"
#include "revenant/recovery/RecoverySink.hpp"
#include "support/FixtureContent.hpp"
#include "support/RecordingProgress.hpp"
#include "support/TempDir.hpp"
#include "support/TempFile.hpp"

namespace {

using revenant::ImageFileDevice;
using revenant::Sha256;
using revenant::Sha256Digest;
using revenant::carve::CarverRegistry;
using revenant::carve::registerBuiltinCarvers;
using revenant::carve::ScanConfig;
using revenant::carve::SignatureScanner;
using revenant::imagegen::ntfs::buildNtfsImage;
using revenant::recovery::arbitrateIndex;
using revenant::recovery::CandidateIndex;
using revenant::recovery::freshRun;
using revenant::recovery::HybridRecovery;
using revenant::recovery::IndexingCandidateVisitor;
using revenant::recovery::IndexingEntryVisitor;
using revenant::recovery::RecoveryMode;
using revenant::recovery::RecoverySink;
using revenant::testing::readFileBytes;
using revenant::testing::RecordingProgress;
using revenant::testing::TempDir;
using revenant::testing::TempFile;

[[nodiscard]] Sha256Digest digestOf(const std::filesystem::path& path) {
	const std::vector<std::byte> bytes = readFileBytes(path);
	Sha256 hash;
	hash.update(bytes);
	return hash.finish();
}

[[nodiscard]] std::unique_ptr<ImageFileDevice> openDevice(const TempFile& file) {
	return std::move(ImageFileDevice::open(file.path()).value());
}

[[nodiscard]] CarverRegistry builtinRegistry() {
	CarverRegistry registry;
	registerBuiltinCarvers(registry);
	return registry;
}

// A hybrid run over the fixture — the widest path the tool has, so the source is
// touched by every reader it owns.
class SourceUnchanged : public ::testing::Test {
protected:
	SourceUnchanged()
		: image_(buildNtfsImage()), device_(openDevice(image_)), registry_(builtinRegistry()),
		  scanner_(registry_, ScanConfig{}) {}

	[[nodiscard]] const TempFile& image() const noexcept {
		return image_;
	}

	[[nodiscard]] std::uint64_t filesWritten() const noexcept {
		return filesWritten_;
	}

	void recoverEverything() {
		discover();
		extractWinners();
	}

private:
	void discover() {
		auto index = CandidateIndex::create(session_.path());
		IndexingEntryVisitor entries{index.value()};
		IndexingCandidateVisitor candidates{index.value()};
		runHybrid(entries, candidates);
	}

	void runHybrid(IndexingEntryVisitor& entries, IndexingCandidateVisitor& candidates) {
		const HybridRecovery recovery{scanner_, freshRun(RecoveryMode::kHybrid)};
		RecordingProgress progress;
		EXPECT_TRUE(recovery.run(*device_, entries, candidates, progress).hasValue());
	}

	void extractWinners() {
		auto decided = arbitrateIndex(session_.path());
		ASSERT_TRUE(decided.hasValue());
		writeWinners(decided.value().winners);
	}

	void writeWinners(const std::vector<revenant::recovery::Candidate>& winners) {
		auto sink = RecoverySink::open(output_.path(), image_.path());
		filesWritten_ = sink.value().extract(winners, *device_).stats.filesWritten;
	}

	TempFile image_;
	std::unique_ptr<ImageFileDevice> device_;
	CarverRegistry registry_;
	SignatureScanner scanner_;
	TempDir session_;
	TempDir output_;
	std::uint64_t filesWritten_{};
};

TEST_F(SourceUnchanged, AFullRecoveryLeavesTheSourceByteForByteIdentical) {
	const Sha256Digest before = digestOf(image().path());
	const auto sizeBefore = std::filesystem::file_size(image().path());

	recoverEverything();

	// An unchanged source proves nothing if the run did no work.
	EXPECT_GT(filesWritten(), 0U);
	EXPECT_EQ(std::filesystem::file_size(image().path()), sizeBefore);
	EXPECT_EQ(digestOf(image().path()), before);
}

} // namespace
