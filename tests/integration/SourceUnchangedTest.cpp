// SPDX-License-Identifier: GPL-3.0-or-later
// The guarantee the whole tool rests on, asserted rather than promised: a full
// recovery run must leave the source byte-for-byte as it found it (ADR-0005).
// This fails if a run writes to its source. It does not police the open flags:
// relaxing those alone writes nothing, so nothing here would notice. Those rest
// on the structural argument — BlockDevice declares no write operation, so no
// layer above the I/O boundary can express a write to the source (ADR-0005).
#include <gtest/gtest.h>

#include <filesystem>

#include "revenant/core/Sha256.hpp"
#include "support/FixtureContent.hpp"
#include "support/RecoveryPipeline.hpp"

namespace {

using revenant::sha256;
using revenant::Sha256Digest;
using revenant::testing::readFileBytes;

[[nodiscard]] Sha256Digest digestOf(const std::filesystem::path& path) {
	return sha256(readFileBytes(path));
}

using SourceUnchanged = revenant::testing::RecoveryPipeline;

TEST_F(SourceUnchanged, AFullRecoveryLeavesTheSourceByteForByteIdentical) {
	const Sha256Digest before = digestOf(image().path());
	const auto sizeBefore = std::filesystem::file_size(image().path());

	runFullRecovery();

	// An unchanged source proves nothing if the run did no work.
	EXPECT_GT(stats().filesWritten, 0U);
	EXPECT_EQ(std::filesystem::file_size(image().path()), sizeBefore);
	EXPECT_EQ(digestOf(image().path()), before);
}

} // namespace
