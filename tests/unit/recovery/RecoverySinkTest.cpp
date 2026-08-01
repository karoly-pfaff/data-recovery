// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/RecoverySink.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <vector>

#include "revenant/core/Confidence.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Sha256.hpp"
#include "revenant/fs/Types.hpp"
#include "revenant/recovery/ArtifactRecord.hpp"
#include "revenant/recovery/Candidate.hpp"
#include "support/InMemoryDevice.hpp"
#include "support/TempDir.hpp"
#include "support/TempFile.hpp"

namespace {

using revenant::Confidence;
using revenant::ErrorCode;
using revenant::fs::Extent;
using revenant::fs::Timestamps;
using revenant::recovery::Candidate;
using revenant::recovery::CandidateSource;
using revenant::recovery::RecoverySink;
using revenant::testing::InMemoryDevice;
using revenant::testing::TempDir;
using revenant::testing::TempFile;

constexpr std::uint32_t kSector = 512;
constexpr std::size_t kDeviceBytes = 4096;

// A device whose every byte is its own offset modulo 251, so any misplaced
// read shows up as different content rather than as more zeroes.
[[nodiscard]] std::vector<std::byte> patternedDevice() {
	std::vector<std::byte> bytes(kDeviceBytes, std::byte{0});
	for (std::size_t at = 0; at < bytes.size(); ++at) {
		bytes.at(at) = static_cast<std::byte>(at % 251);
	}
	return bytes;
}

[[nodiscard]] std::vector<std::byte> deviceRange(std::size_t offset, std::size_t length) {
	const auto bytes = patternedDevice();
	return std::vector<std::byte>{
		bytes.begin() + static_cast<std::ptrdiff_t>(offset),
		bytes.begin() + static_cast<std::ptrdiff_t>(offset + length)};
}

[[nodiscard]] Candidate entryAt(std::string_view path, const std::vector<Extent>& extents) {
	return Candidate{
		.name = std::string{path},
		.extents = extents,
		.residentContent = {},
		.timestamps = Timestamps{.created = 0, .modified = 0, .accessed = 0},
		.confidence = Confidence::kValid,
		.source = CandidateSource::kFilesystem};
}

// A named entry whose content sits inside its own metadata record.
struct Resident {
	std::string_view path;
	std::string_view content;
};

[[nodiscard]] Candidate residentEntry(const Resident& entry) {
	std::vector<std::byte> bytes;
	for (const char letter : entry.content) {
		bytes.push_back(std::bit_cast<std::byte>(letter));
	}
	return Candidate{
		.name = std::string{entry.path},
		.extents = {},
		.residentContent = bytes,
		.timestamps = Timestamps{.created = 0, .modified = 0, .accessed = 0},
		.confidence = Confidence::kValid,
		.source = CandidateSource::kFilesystem};
}

// The carved counterpart of `entryAt`: no name of its own, just a bucket.
[[nodiscard]] Candidate carvedAt(std::uint64_t offset, std::uint64_t length) {
	return Candidate{
		.name = "jpg",
		.extents = {Extent{.deviceOffset = offset, .lengthBytes = length}},
		.residentContent = {},
		.timestamps = Timestamps{.created = 0, .modified = 0, .accessed = 0},
		.confidence = Confidence::kValid,
		.source = CandidateSource::kCarve};
}

// The extent `carvedAt(100, 50)` covers, so a named entry can be made to hold
// byte-for-byte what a carved one does.
[[nodiscard]] Extent sameBytes() {
	return Extent{.deviceOffset = 100, .lengthBytes = 50};
}

[[nodiscard]] std::vector<std::byte> bytesOf(std::string_view text) {
	std::vector<std::byte> bytes;
	for (const char letter : text) {
		bytes.push_back(std::bit_cast<std::byte>(letter));
	}
	return bytes;
}

[[nodiscard]] std::vector<std::byte> fileBytes(const std::filesystem::path& path) {
	std::ifstream stream{path, std::ios::binary};
	std::vector<std::byte> bytes;
	for (auto value = stream.get(); value != std::char_traits<char>::eof(); value = stream.get()) {
		bytes.push_back(std::bit_cast<std::byte>(static_cast<char>(value)));
	}
	return bytes;
}

// One sink over a fresh destination, with the patterned device behind it. The
// source is a real image file beside the destination rather than a spelling:
// ADR-0005's rule now asks what a source *is*, and only an image is judged by
// its path (story-0609).
class Sink {
public:
	explicit Sink(const TempDir& destination)
		: device_(patternedDevice(), kSector), source_(patternedDevice()),
		  sink_(RecoverySink::open(destination.path(), source_.path()).value()) {}

	[[nodiscard]] revenant::recovery::ExtractionStats
	extract(const std::vector<Candidate>& winners) {
		return sink_.extract(winners, device_).stats;
	}

	// The whole extraction, for the tests that are about what it recorded
	// rather than about what it counted.
	[[nodiscard]] revenant::recovery::Extraction extractAll(const std::vector<Candidate>& winners) {
		return sink_.extract(winners, device_);
	}

	[[nodiscard]] revenant::recovery::Extraction preview(const std::vector<Candidate>& winners) {
		return sink_.preview(winners);
	}

private:
	InMemoryDevice device_;
	TempFile source_;
	RecoverySink sink_;
};

TEST(RecoverySink, RefusesADestinationThatIsNotThere) {
	const auto opened = RecoverySink::open(
		std::filesystem::path{"D:/definitely/not/here"},
		std::filesystem::path{"D:/src.img"});
	ASSERT_FALSE(opened.hasValue());
	EXPECT_EQ(opened.error().code, ErrorCode::kNotFound);
}

TEST(RecoverySink, RefusesAFileAsADestination) {
	TempDir directory;
	const auto file = directory.path() / "not-a-directory";
	std::ofstream{file, std::ios::binary} << "x";
	const auto opened = RecoverySink::open(file, std::filesystem::path{"D:/src.img"});
	ASSERT_FALSE(opened.hasValue());
	EXPECT_EQ(opened.error().code, ErrorCode::kNotFound);
}

// Recovered data must not be written onto the media being recovered.
TEST(RecoverySink, RefusesADestinationHoldingTheSource) {
	TempDir directory;
	const auto opened = RecoverySink::open(directory.path(), directory.path() / "disk.img");
	ASSERT_FALSE(opened.hasValue());
	EXPECT_EQ(opened.error().code, ErrorCode::kInvalidArgument);
}

// The device tier, reached through the sink: a source that is not a regular
// file is judged by physical identity rather than by spelling, and an identity
// that cannot be answered refuses the run instead of being read as "somewhere
// else" (story-0609).
TEST(RecoverySink, RefusesADeviceSourceWhoseStorageCannotBeNamed) {
	TempDir directory;
	const auto opened = RecoverySink::open(directory.path(), std::filesystem::path{"no-such-dev"});
	ASSERT_FALSE(opened.hasValue());
	EXPECT_EQ(opened.error().code, ErrorCode::kDestinationOnSource);
}

TEST(RecoverySink, WritesResidentContentWithoutTouchingTheDevice) {
	TempDir directory;
	Sink sink{directory};
	const auto stats = sink.extract({residentEntry({.path = "notes.txt", .content = "hello"})});
	EXPECT_EQ(stats.filesWritten, 1U);
	EXPECT_EQ(stats.bytesWritten, 5U);
	EXPECT_EQ(fileBytes(directory.path() / "notes.txt").size(), 5U);
}

TEST(RecoverySink, ReconstructsANamedEntrysDirectoryTree) {
	TempDir directory;
	Sink sink{directory};
	EXPECT_EQ(
		sink.extract({residentEntry({.path = "photos/2024/notes.txt", .content = "hi"})})
			.filesWritten,
		1U);
	EXPECT_TRUE(std::filesystem::exists(directory.path() / "photos" / "2024" / "notes.txt"));
}

// The extents are read in file order, so a fragmented file comes back as the
// file rather than as its first run.
TEST(RecoverySink, ReadsAFragmentedWinnerThroughItsExtentsInOrder) {
	TempDir directory;
	Sink sink{directory};
	const auto winner = entryAt(
		"photos/split.bin",
		{Extent{.deviceOffset = 2000, .lengthBytes = 100},
		 Extent{.deviceOffset = 100, .lengthBytes = 50}});
	EXPECT_EQ(sink.extract({winner}).bytesWritten, 150U);
	auto expected = deviceRange(2000, 100);
	const auto tail = deviceRange(100, 50);
	expected.insert(expected.end(), tail.begin(), tail.end());
	EXPECT_EQ(fileBytes(directory.path() / "photos" / "split.bin"), expected);
}

TEST(RecoverySink, DisambiguatesTwoWinnersWantingOneNameAndSaysSo) {
	TempDir directory;
	Sink sink{directory};
	const auto stats = sink.extract(
		{residentEntry({.path = "notes.txt", .content = "first"}),
		 residentEntry({.path = "notes.txt", .content = "second"})});
	EXPECT_EQ(stats.filesWritten, 2U);
	EXPECT_EQ(stats.renamed, 1U);
	EXPECT_TRUE(std::filesystem::exists(directory.path() / "notes.txt"));
	EXPECT_TRUE(std::filesystem::exists(directory.path() / "notes (2).txt"));
}

// Nothing safe survives the name, so nothing is written — least of all outside
// the destination.
TEST(RecoverySink, RefusesAnEscapingNameAndCountsIt) {
	TempDir directory;
	Sink sink{directory};
	const auto stats = sink.extract({residentEntry({.path = "../escaped.txt", .content = "no"})});
	EXPECT_EQ(stats.filesWritten, 0U);
	EXPECT_EQ(stats.failed, 1U);
	EXPECT_FALSE(std::filesystem::exists(directory.path().parent_path() / "escaped.txt"));
}

// A device that does not hold what the metadata claimed is a failed recovery,
// not a shorter file that looks complete.
TEST(RecoverySink, CountsAWinnerReachingPastTheDeviceAsFailed) {
	TempDir directory;
	Sink sink{directory};
	const auto winner =
		entryAt("truncated.bin", {Extent{.deviceOffset = kDeviceBytes - 10, .lengthBytes = 500}});
	const auto stats = sink.extract({winner});
	EXPECT_EQ(stats.failed, 1U);
	EXPECT_EQ(stats.filesWritten, 0U);
}

TEST(RecoverySink, NamesCarvedWinnersByTheirBucketAndOrdinal) {
	TempDir directory;
	Sink sink{directory};
	EXPECT_EQ(sink.extract({carvedAt(0, 16)}).filesWritten, 1U);
	EXPECT_TRUE(std::filesystem::exists(directory.path() / "carved" / "jpg" / "f00000000.jpg"));
}

// Names are strictly better than `f0000001.jpg`, so the anonymous copy of
// something already recovered under a name is the one that goes.
TEST(RecoverySink, DropsACarvedWinnerHoldingBytesAlreadyRecoveredUnderAName) {
	TempDir directory;
	Sink sink{directory};
	const auto stats = sink.extract({carvedAt(100, 50), entryAt("photos/same.bin", {sameBytes()})});
	EXPECT_EQ(stats.filesWritten, 1U);
	EXPECT_EQ(stats.deduplicated, 1U);
	EXPECT_TRUE(std::filesystem::exists(directory.path() / "photos" / "same.bin"));
	EXPECT_FALSE(std::filesystem::exists(directory.path() / "carved" / "jpg" / "f00000000.jpg"));
}

// Two real files with two real names happen to hold the same bytes. Dropping
// either would be data loss dressed up as tidiness.
TEST(RecoverySink, NeverDropsANamedWinnerForDuplicatingAnother) {
	TempDir directory;
	Sink sink{directory};
	const auto stats = sink.extract(
		{residentEntry({.path = "a.txt", .content = "same"}),
		 residentEntry({.path = "b.txt", .content = "same"})});
	EXPECT_EQ(stats.filesWritten, 2U);
	EXPECT_EQ(stats.deduplicated, 0U);
}

// Named artifacts are written first, but numbered where they stand: a carved
// winner's name must not depend on the order it happened to be written in.
TEST(RecoverySink, NumbersCarvedWinnersInDeviceOrderWhateverTheWriteOrder) {
	TempDir directory;
	Sink sink{directory};
	const auto stats = sink.extract(
		{carvedAt(0, 16), residentEntry({.path = "notes.txt", .content = "hi"}), carvedAt(64, 16)});
	EXPECT_EQ(stats.filesWritten, 3U);
	EXPECT_TRUE(std::filesystem::exists(directory.path() / "carved" / "jpg" / "f00000000.jpg"));
	EXPECT_TRUE(std::filesystem::exists(directory.path() / "carved" / "jpg" / "f00000002.jpg"));
}

TEST(RecoverySink, RecordsWhatEachArtifactHoldsForTheManifest) {
	TempDir directory;
	Sink sink{directory};
	const auto extraction =
		sink.extractAll({residentEntry({.path = "notes.txt", .content = "hello"})});
	ASSERT_EQ(extraction.artifacts.size(), 1U);
	const auto& artifact = extraction.artifacts.front();
	EXPECT_EQ(artifact.writtenName, "notes.txt");
	EXPECT_EQ(artifact.outcome, revenant::recovery::ArtifactOutcome::kWritten);
	EXPECT_EQ(artifact.contentHash, revenant::toHex(revenant::sha256(bytesOf("hello"))));
}

// What a composed stack cannot absorb still fails, and is still counted. A
// refused *sector* no longer reaches the sink at all — the retry layer answers
// it with zeros and records the range (story-0604) — but an extent running off
// the end of the device is not damage the stack can invent its way past.
TEST(RecoverySink, AWinnerItCannotReadIsCountedAsFailed) {
	TempDir directory;
	Sink sink{directory};
	const auto winner =
		entryAt("truncated.bin", {Extent{.deviceOffset = kDeviceBytes - 10, .lengthBytes = 500}});
	const auto extraction = sink.extractAll({winner});
	EXPECT_EQ(extraction.stats.failed, 1U);
	ASSERT_EQ(extraction.artifacts.size(), 1U);
	EXPECT_EQ(extraction.artifacts.front().outcome, revenant::recovery::ArtifactOutcome::kFailed);
	EXPECT_TRUE(extraction.artifacts.front().contentHash.empty());
}

// ADR-0006 already separated deciding from writing, so a preview is the same
// run one step shorter — and the names it reports are the names the run would
// use, not a second guess at them.
TEST(RecoverySink, PreviewsEveryWinnerUnderTheNameItWouldBeWrittenAs) {
	TempDir directory;
	Sink sink{directory};
	const auto previewed = sink.preview(
		{residentEntry({.path = "photos/notes.txt", .content = "hi"}), carvedAt(0, 16)});
	ASSERT_EQ(previewed.artifacts.size(), 2U);
	EXPECT_EQ(previewed.artifacts.front().writtenName, "photos/notes.txt");
	EXPECT_EQ(previewed.artifacts.back().writtenName, "carved/jpg/f00000001.jpg");
	EXPECT_EQ(previewed.artifacts.front().outcome, revenant::recovery::ArtifactOutcome::kPreviewed);
}

TEST(RecoverySink, APreviewCreatesNothingAtAll) {
	TempDir directory;
	Sink sink{directory};
	const auto previewed =
		sink.preview({residentEntry({.path = "photos/notes.txt", .content = "hi"})});
	EXPECT_EQ(previewed.stats.filesWritten, 0U);
	EXPECT_EQ(previewed.stats.bytesWritten, 0U);
	EXPECT_FALSE(std::filesystem::exists(directory.path() / "photos"));
	EXPECT_TRUE(previewed.artifacts.front().contentHash.empty());
}

TEST(RecoverySink, APreviewDisambiguatesTheSameWayAWriteWould) {
	TempDir directory;
	Sink sink{directory};
	const auto previewed = sink.preview(
		{residentEntry({.path = "notes.txt", .content = "first"}),
		 residentEntry({.path = "notes.txt", .content = "second"})});
	EXPECT_EQ(previewed.stats.renamed, 1U);
	EXPECT_EQ(previewed.artifacts.back().writtenName, "notes (2).txt");
}

TEST(RecoverySink, APreviewCountsAWinnerNothingSafeSurvives) {
	TempDir directory;
	Sink sink{directory};
	const auto previewed =
		sink.preview({residentEntry({.path = "../escaped.txt", .content = "no"})});
	EXPECT_EQ(previewed.stats.failed, 1U);
	EXPECT_EQ(previewed.artifacts.front().outcome, revenant::recovery::ArtifactOutcome::kFailed);
	EXPECT_TRUE(previewed.artifacts.front().writtenName.empty());
}

} // namespace
