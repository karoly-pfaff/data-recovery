// SPDX-License-Identifier: GPL-3.0-or-later
// The guarantee the whole tool rests on, asserted rather than promised: a full
// recovery run must leave the source byte-for-byte as it found it (ADR-0005).
// Everything above the I/O layer is structurally incapable of writing to it —
// BlockDevice declares no write operation — and every open goes through
// openReadOnly. This test is what fails if either of those stops being true.
#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <vector>

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
