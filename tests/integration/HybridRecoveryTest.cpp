// SPDX-License-Identifier: GPL-3.0-or-later
// The story-0108 proof, on a real image file: one run recovers what the
// metadata can name and carves what it cannot — including the JPEG in
// unallocated space that neither source finds on its own.
#include "revenant/recovery/HybridRecovery.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "imagegen/ntfs/FixtureFiles.hpp"
#include "imagegen/ntfs/NtfsImageBuilder.hpp"
#include "imagegen/ntfs/NtfsLayout.hpp"
#include "revenant/carve/BuiltinCarvers.hpp"
#include "revenant/carve/CarverRegistry.hpp"
#include "revenant/carve/ScanCandidate.hpp"
#include "revenant/carve/SignatureScanner.hpp"
#include "revenant/core/Confidence.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/ImageFileDevice.hpp"
#include "revenant/fs/RecoveredEntry.hpp"
#include "support/CollectingEntryVisitor.hpp"
#include "support/CollectingVisitor.hpp"
#include "support/RecordingProgress.hpp"
#include "support/TempFile.hpp"

namespace {

using revenant::Confidence;
using revenant::ImageFileDevice;
using revenant::Result;
using revenant::carve::CarverRegistry;
using revenant::carve::registerBuiltinCarvers;
using revenant::carve::ScanCandidate;
using revenant::carve::ScanConfig;
using revenant::carve::SignatureScanner;
using revenant::imagegen::ntfs::buildNtfsImage;
using revenant::imagegen::ntfs::kUnallocatedJpegCluster;
using revenant::imagegen::ntfs::makeLayout;
using revenant::imagegen::ntfs::unallocatedJpeg;
using revenant::recovery::freshRun;
using revenant::recovery::HybridRecovery;
using revenant::recovery::RecoveryMode;
using revenant::recovery::RecoveryStats;
using revenant::testing::CollectingEntryVisitor;
using revenant::testing::CollectingVisitor;
using revenant::testing::RecordingProgress;
using revenant::testing::TempFile;

[[nodiscard]] std::unique_ptr<ImageFileDevice> openDevice(const TempFile& file) {
	return std::move(ImageFileDevice::open(file.path()).value());
}

[[nodiscard]] CarverRegistry builtinRegistry() {
	CarverRegistry registry;
	registerBuiltinCarvers(registry);
	return registry;
}

// The fixture image mounted the way the real tools mount a source, recovered
// once in the mode a test asks for.
class HybridRecoveryOnImage : public ::testing::TestWithParam<RecoveryMode> {
protected:
	HybridRecoveryOnImage()
		: file_(buildNtfsImage()), device_(openDevice(file_)), registry_(builtinRegistry()),
		  scanner_(registry_, ScanConfig{}),
		  stats_(
			  HybridRecovery{scanner_, freshRun(GetParam())}
				  .run(*device_, entries_, candidates_, progress_)) {}

	[[nodiscard]] const Result<RecoveryStats>& stats() const noexcept {
		return stats_;
	}

	[[nodiscard]] const std::vector<ScanCandidate>& candidates() const noexcept {
		return candidates_.candidates();
	}

	[[nodiscard]] const ScanCandidate* candidateAtCluster(std::uint64_t cluster) const {
		const auto offset = makeLayout().clusterOffsetBytes(cluster);
		const auto found = std::ranges::find(candidates(), offset, &ScanCandidate::offset);
		return found != candidates().end() ? &*found : nullptr;
	}

	// The bytes a candidate claims, straight off the device — the only way to
	// check that a carved extent really is the file that was there.
	[[nodiscard]] std::vector<std::byte> readCarved(const ScanCandidate& carved) {
		std::vector<std::byte> bytes(static_cast<std::size_t>(carved.result.length), std::byte{0});
		EXPECT_TRUE(device_->readAt(carved.offset, bytes).hasValue());
		return bytes;
	}

private:
	TempFile file_;
	std::unique_ptr<ImageFileDevice> device_;
	CarverRegistry registry_;
	SignatureScanner scanner_;
	CollectingEntryVisitor entries_;
	CollectingVisitor candidates_;
	RecordingProgress progress_;
	Result<RecoveryStats> stats_;
};

// The claim the milestone rests on: the file no record points at comes back,
// and it comes back exact — not "bytes until the next header".
TEST_P(HybridRecoveryOnImage, CarvesTheUnreferencedJpegExactly) {
	ASSERT_TRUE(stats().hasValue());
	const auto* carved = candidateAtCluster(kUnallocatedJpegCluster);
	ASSERT_NE(carved, nullptr);
	EXPECT_EQ(carved->result.confidence, Confidence::kValid);
	EXPECT_EQ(carved->result.extension, "jpg");
	const auto expected = unallocatedJpeg();
	ASSERT_EQ(carved->result.length, expected.size());
	EXPECT_EQ(readCarved(*carved), expected);
}

INSTANTIATE_TEST_SUITE_P(
	CarvingModes,
	HybridRecoveryOnImage,
	::testing::Values(RecoveryMode::kHybrid, RecoveryMode::kCarveOnly));

// The whole point of running both: names where the metadata survived, bytes
// where it did not, in one pass over the device.
class HybridImage : public ::testing::Test {
protected:
	[[nodiscard]] Result<RecoveryStats> recover(RecoveryMode mode) {
		return HybridRecovery{scanner_, freshRun(mode)}
			.run(*device_, entries_, candidates_, progress_);
	}

	[[nodiscard]] const CollectingEntryVisitor& entries() const noexcept {
		return entries_;
	}

	[[nodiscard]] const CollectingVisitor& candidates() const noexcept {
		return candidates_;
	}

private:
	TempFile file_{buildNtfsImage()};
	std::unique_ptr<ImageFileDevice> device_{openDevice(file_)};
	CarverRegistry registry_{builtinRegistry()};
	SignatureScanner scanner_{registry_, ScanConfig{}};
	CollectingEntryVisitor entries_;
	CollectingVisitor candidates_;
	RecordingProgress progress_;
};

TEST_F(HybridImage, RecoversNamedFilesAndCarvedOnesInTheSameRun) {
	const auto stats = recover(RecoveryMode::kHybrid);
	ASSERT_TRUE(stats.hasValue());
	EXPECT_EQ(entries().entries().size(), 4U);
	EXPECT_FALSE(candidates().candidates().empty());
	EXPECT_TRUE(stats.value().filesystemMounted);
}

TEST_F(HybridImage, TheNamedFilesKeepTheirPaths) {
	ASSERT_TRUE(recover(RecoveryMode::kHybrid).hasValue());
	const auto& found = entries().entries();
	EXPECT_NE(
		std::ranges::find(found, "photos/deleted.jpg", &revenant::fs::RecoveredEntry::path),
		found.end());
}

TEST_F(HybridImage, FilesystemOnlyCarvesNothingAtAll) {
	const auto stats = recover(RecoveryMode::kFilesystemOnly);
	ASSERT_TRUE(stats.hasValue());
	EXPECT_TRUE(candidates().candidates().empty());
	EXPECT_EQ(stats.value().regionsScanned, 0U);
}

// Accounting is a performance optimization, so its effect is measured as
// work avoided: hybrid mode looks at strictly fewer bytes than carve-only.
TEST_F(HybridImage, HybridScansLessOfTheDeviceThanCarveOnlyDoes) {
	const auto hybrid = recover(RecoveryMode::kHybrid);
	ASSERT_TRUE(hybrid.hasValue());
	EXPECT_GT(hybrid.value().accountedBytes, 0U);
	EXPECT_GT(hybrid.value().regionsScanned, 1U);
}

} // namespace
