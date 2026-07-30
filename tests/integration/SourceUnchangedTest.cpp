// SPDX-License-Identifier: GPL-3.0-or-later
// The guarantee the whole tool rests on, asserted rather than promised: a full
// recovery run must leave the source byte-for-byte as it found it (ADR-0005).
// Everything above the I/O layer is structurally incapable of writing to it —
// BlockDevice declares no write operation — and every open goes through
// openReadOnly. This test is what fails if either of those stops being true.
#include <gtest/gtest.h>

#include <cstddef>
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

// A hybrid run over the fixture: mount, carve, index, arbitrate, extract. The
// widest path the tool has, so the source is touched by every reader it owns.
void recoverEverything(const TempFile& image, const TempDir& session, const TempDir& output) {
	auto device = ImageFileDevice::open(image.path());
	ASSERT_TRUE(device.hasValue());

	CarverRegistry registry;
	registerBuiltinCarvers(registry);
	const SignatureScanner scanner{registry, ScanConfig{}};

	auto index = CandidateIndex::create(session.path());
	ASSERT_TRUE(index.hasValue());
	IndexingEntryVisitor entries{index.value()};
	IndexingCandidateVisitor candidates{index.value()};
	RecordingProgress progress;
	const HybridRecovery recovery{scanner, freshRun(RecoveryMode::kHybrid)};
	ASSERT_TRUE(recovery.run(*device.value(), entries, candidates, progress).hasValue());

	auto decided = arbitrateIndex(session.path());
	ASSERT_TRUE(decided.hasValue());
	auto sink = RecoverySink::open(output.path(), image.path());
	ASSERT_TRUE(sink.hasValue());
	const auto extracted = sink.value().extract(decided.value().winners, *device.value());
	// The run has to have done real work, or an unchanged source proves nothing.
	EXPECT_GT(extracted.stats.filesWritten, 0U);
}

TEST(SourceUnchanged, AFullRecoveryLeavesTheSourceByteForByteIdentical) {
	const TempFile image{buildNtfsImage()};
	const Sha256Digest before = digestOf(image.path());
	const auto sizeBefore = std::filesystem::file_size(image.path());

	const TempDir session;
	const TempDir output;
	recoverEverything(image, session, output);

	EXPECT_EQ(std::filesystem::file_size(image.path()), sizeBefore);
	EXPECT_EQ(digestOf(image.path()), before);
}

} // namespace
