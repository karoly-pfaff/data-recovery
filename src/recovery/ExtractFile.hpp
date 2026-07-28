// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Writing one winner's bytes to one already-sanitized path. Split
// from the sink because deciding *where* a file goes and getting its bytes
// there are two jobs. Not a public interface.

#include <cstdint>
#include <filesystem>

#include "revenant/core/Result.hpp"
#include "revenant/core/Sha256.hpp"
#include "revenant/core/io/BlockDevice.hpp"
#include "revenant/recovery/Candidate.hpp"

namespace revenant::recovery {

// What one artifact turned out to be: its size, and the digest of the bytes
// that reached the destination.
struct ExtractedFile {
	std::uint64_t bytes;
	Sha256Digest content;
};

// Writes `winner` to `target`, creating the directories it needs, and returns
// what landed there. Content carried by the candidate is written as it is;
// everything else is read back through the winner's extents in bounded chunks,
// because a winner's extents come off a disk and may not size an allocation
// (ADR-0009). A read that comes up short is a typed error, not a shorter file.
//
// The digest is taken of the bytes on their way past, so the manifest costs no
// second pass over the recovered data.
[[nodiscard]] Result<ExtractedFile>
extractTo(const std::filesystem::path& target, const Candidate& winner, BlockDevice& device);

} // namespace revenant::recovery
