// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/Checkpoint.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <string_view>

#include "revenant/core/Error.hpp"
#include "revenant/core/Sha256.hpp"
#include "support/TempDir.hpp"

namespace {

using revenant::ErrorCode;
using revenant::sha256;
using revenant::recovery::Checkpoint;
using revenant::recovery::clearCheckpoint;
using revenant::recovery::kCheckpointFileName;
using revenant::recovery::readCheckpoint;
using revenant::recovery::writeCheckpoint;
using revenant::testing::TempDir;

[[nodiscard]] revenant::Sha256Digest someShape() {
	const std::string description = "hybrid:4194304";
	return sha256(std::as_bytes(std::span{description}));
}

[[nodiscard]] Checkpoint someCheckpoint() {
	return Checkpoint{.shape = someShape(), .scanCursor = 1 << 20U, .indexRecords = 7};
}

void overwriteWith(const std::filesystem::path& path, std::string_view content) {
	std::ofstream stream{path, std::ios::binary | std::ios::trunc};
	stream << content;
}

// The stored checkpoint with one byte changed at `at`, which is how a foreign
// magic and a bumped version are both stated.
void damageByteAt(const std::filesystem::path& path, std::streamoff at) {
	std::fstream stream{path, std::ios::binary | std::ios::in | std::ios::out};
	stream.seekp(at);
	stream.put('\xFF');
}

TEST(Checkpoint, ComesBackExactlyAsItWasWritten) {
	const TempDir session;
	ASSERT_TRUE(writeCheckpoint(session.path(), someCheckpoint()).hasValue());
	const auto read = readCheckpoint(session.path());
	ASSERT_TRUE(read.hasValue());
	EXPECT_EQ(read.value(), someCheckpoint());
}

TEST(Checkpoint, ReplacingOneLeavesTheNewer) {
	const TempDir session;
	ASSERT_TRUE(writeCheckpoint(session.path(), someCheckpoint()).hasValue());
	Checkpoint later = someCheckpoint();
	later.scanCursor = 1 << 24U;
	ASSERT_TRUE(writeCheckpoint(session.path(), later).hasValue());
	EXPECT_EQ(readCheckpoint(session.path()).value().scanCursor, later.scanCursor);
}

TEST(Checkpoint, AMissingOneIsNotFound) {
	const TempDir session;
	const auto read = readCheckpoint(session.path());
	ASSERT_FALSE(read.hasValue());
	EXPECT_EQ(read.error().code, ErrorCode::kNotFound);
}

// Session state is untrusted on reload like any other bytes (ADR-0009).
TEST(Checkpoint, RefusesAFileThatIsNotOneOfOurs) {
	const TempDir session;
	ASSERT_TRUE(writeCheckpoint(session.path(), someCheckpoint()).hasValue());
	damageByteAt(session.path() / kCheckpointFileName, 0);
	EXPECT_EQ(readCheckpoint(session.path()).error().code, ErrorCode::kInvalidArgument);
}

TEST(Checkpoint, RefusesAVersionItDoesNotKnow) {
	const TempDir session;
	ASSERT_TRUE(writeCheckpoint(session.path(), someCheckpoint()).hasValue());
	damageByteAt(session.path() / kCheckpointFileName, 8);
	EXPECT_EQ(readCheckpoint(session.path()).error().code, ErrorCode::kInvalidArgument);
}

TEST(Checkpoint, RefusesOneThatIsTooShortToBeWhole) {
	const TempDir session;
	overwriteWith(session.path() / kCheckpointFileName, "RVNTCKP");
	EXPECT_EQ(readCheckpoint(session.path()).error().code, ErrorCode::kInvalidArgument);
}

// A fresh run must not be mistaken for a resumable one because a previous
// session left its cursor behind.
TEST(Checkpoint, ClearingOneLeavesNothingToResumeFrom) {
	const TempDir session;
	ASSERT_TRUE(writeCheckpoint(session.path(), someCheckpoint()).hasValue());
	clearCheckpoint(session.path());
	EXPECT_EQ(readCheckpoint(session.path()).error().code, ErrorCode::kNotFound);
}

} // namespace
