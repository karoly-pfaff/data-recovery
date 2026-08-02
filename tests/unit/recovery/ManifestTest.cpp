// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/Manifest.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "revenant/core/Confidence.hpp"
#include "revenant/core/io/BadRange.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/recovery/ArtifactRecord.hpp"
#include "revenant/recovery/Candidate.hpp"
#include "revenant/recovery/HybridRecovery.hpp"
#include "support/FixtureContent.hpp"
#include "support/TempDir.hpp"

namespace {

using revenant::BadRange;
using revenant::Confidence;
using revenant::fs::Extent;
using revenant::fs::Timestamps;
using revenant::recovery::ArtifactOutcome;
using revenant::recovery::ArtifactRecord;
using revenant::recovery::CandidateSource;
using revenant::recovery::kManifestFileName;
using revenant::recovery::manifestJson;
using revenant::recovery::RecoveryMode;
using revenant::recovery::SessionManifest;
using revenant::recovery::writeManifest;
using revenant::testing::readFileText;
using revenant::testing::TempDir;

[[nodiscard]] ArtifactRecord namedArtifact() {
	return ArtifactRecord{
		.originalName = "photos/deleted.jpg",
		.writtenName = "photos/deleted.jpg",
		.extents = {Extent{.deviceOffset = 4096, .lengthBytes = 1024}},
		.bytes = 1024,
		.contentHash = "abc123",
		.invented = {},
		.timestamps = Timestamps{.created = 1, .modified = 2, .accessed = 3},
		.confidence = Confidence::kValid,
		.source = CandidateSource::kFilesystem,
		.outcome = ArtifactOutcome::kWritten};
}

[[nodiscard]] SessionManifest manifestOf(std::vector<ArtifactRecord> artifacts) {
	return SessionManifest{
		.source = "disk.img",
		.destination = "out",
		.mode = RecoveryMode::kHybrid,
		.winners = 5,
		.suppressed = 1,
		.artifacts = std::move(artifacts),
		.unreadable = {}};
}

[[nodiscard]] bool holds(const std::string& text, const std::string& fragment) {
	return text.find(fragment) != std::string::npos;
}

TEST(Manifest, StatesEveryFactAboutAnArtifact) {
	const auto text = manifestJson(manifestOf({namedArtifact()}));
	EXPECT_TRUE(holds(text, R"("originalName":"photos/deleted.jpg")"));
	EXPECT_TRUE(holds(text, R"("source":"filesystem")"));
	EXPECT_TRUE(holds(text, R"("confidence":"valid")"));
	EXPECT_TRUE(holds(text, R"("sha256":"abc123")"));
	EXPECT_TRUE(holds(text, R"("offset":4096)"));
}

TEST(Manifest, StatesTheRunItself) {
	const auto text = manifestJson(manifestOf({}));
	EXPECT_TRUE(holds(text, R"("mode":"hybrid")"));
	EXPECT_TRUE(holds(text, R"("winners":5)"));
	EXPECT_TRUE(holds(text, R"("suppressed":1)"));
	EXPECT_TRUE(holds(text, R"("artifacts":[])"));
}

TEST(Manifest, NamesTheModeTheWayTheCommandLineDoes) {
	auto manifest = manifestOf({});
	manifest.mode = RecoveryMode::kCarveOnly;
	EXPECT_TRUE(holds(manifestJson(manifest), R"("mode":"carve-only")"));
}

// An artifact that never landed has no hash and no written name to state, and
// inventing either would make the manifest confidently wrong.
TEST(Manifest, AFailedArtifactClaimsNoHashAndNoName) {
	auto artifact = namedArtifact();
	artifact.writtenName.clear();
	artifact.contentHash.clear();
	artifact.bytes = 0;
	artifact.outcome = ArtifactOutcome::kFailed;
	const auto text = manifestJson(manifestOf({artifact}));
	EXPECT_TRUE(holds(text, R"("outcome":"failed")"));
	EXPECT_TRUE(holds(text, R"("sha256":"")"));
	EXPECT_TRUE(holds(text, R"("writtenName":"")"));
}

// A filename is data off a hostile device, so it has to be escaped rather than
// trusted to be well-behaved inside a JSON string.
TEST(Manifest, EscapesANameThatWouldOtherwiseBreakTheDocument) {
	auto artifact = namedArtifact();
	artifact.originalName = "a\"b\\c\nd";
	const auto text = manifestJson(manifestOf({artifact}));
	EXPECT_TRUE(holds(text, "\"originalName\":\"a\\\"b\\\\c\\u000ad\""));
}

// story-0604: ranges, not bare offsets. A reader that survives the fault now
// sits in every run, so the manifest can say how far the damage runs — which is
// the condition story-0115 named when it chose offsets.
TEST(Manifest, RecordsTheRunsBadSectorMapAsRanges) {
	auto manifest = manifestOf({});
	manifest.unreadable = {
		BadRange{.offsetBytes = 512, .lengthBytes = 512},
		BadRange{.offsetBytes = 4096, .lengthBytes = 1024}};
	EXPECT_TRUE(holds(
		manifestJson(manifest),
		R"("unreadable":[{"offset":512,"length":512},{"offset":4096,"length":1024}])"));
}

// An artifact says which of its own bytes were invented, so a file that
// validated on zeros the device never supplied cannot read as clean.
TEST(Manifest, RecordsWhichOfAnArtifactsBytesWereInvented) {
	auto artifact = namedArtifact();
	artifact.invented = {BadRange{.offsetBytes = 4608, .lengthBytes = 512}};
	const auto text = manifestJson(manifestOf({artifact}));
	EXPECT_TRUE(holds(text, R"("invented":[{"offset":4608,"length":512}])"));
}

TEST(Manifest, AnUndamagedArtifactSaysSoWithAnEmptyList) {
	EXPECT_TRUE(holds(manifestJson(manifestOf({namedArtifact()})), R"("invented":[])"));
}

TEST(Manifest, LandsInTheSessionDirectory) {
	const TempDir session;
	const auto written = writeManifest(session.path(), manifestOf({namedArtifact()}));
	ASSERT_TRUE(written.hasValue());
	EXPECT_EQ(written.value(), session.path() / kManifestFileName);
	EXPECT_EQ(readFileText(written.value()), manifestJson(manifestOf({namedArtifact()})));
}

} // namespace
