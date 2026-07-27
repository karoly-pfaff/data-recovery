// SPDX-License-Identifier: GPL-3.0-or-later
// Internal. Locating and validating a ZIP archive's End Of Central Directory
// record, which is what makes its extent exact. Not a public interface.
#pragma once

#include <cstdint>

#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

// A located EOCD record and what it says about the archive.
struct ZipEndRecord {
	std::uint64_t offset = 0;                 // where the EOCD itself starts
	std::uint64_t end = 0;                    // just past its trailing comment
	std::uint64_t centralDirectoryOffset = 0; // where the directory begins
	std::uint32_t entryCount = 0;             // entries in the directory
	bool centralDirectoryChecksOut = false;   // the arithmetic and header agree
};

// Finds the last End Of Central Directory record in `reader` and reads it.
// kNotFound when the data holds none — an archive without its end record has
// no extent this carver is willing to claim.
[[nodiscard]] Result<ZipEndRecord> findZipEndRecord(const ByteReader& reader);

} // namespace revenant::carve
