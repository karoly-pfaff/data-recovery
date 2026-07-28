// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "revenant/core/Confidence.hpp"
#include "revenant/fs/Types.hpp"

namespace revenant::recovery {

// Which source is claiming these bytes. Not merely a label: at equal
// confidence a named entry beats an anonymous carve of the same region,
// because a name is strictly better than `f0000001.jpg`.
enum class CandidateSource : std::uint8_t { kFilesystem, kCarve };

// One hypothesis about some bytes — where they are, how far the claim is
// trusted, what the file would be called, and who is claiming it. A candidate
// is a finding, not a file: nothing has been written anywhere (ADR-0006).
struct Candidate {
	// A filesystem entry's volume-relative path, or a carved file's extension
	// ("jpg"). The sink turns either into a destination name; which of the two
	// this is follows from `source`.
	std::string name;

	// Where the bytes live. Empty for content small enough to sit inside its
	// own metadata record, which is then carried in `residentContent` —
	// exactly one of the two is populated.
	std::vector<fs::Extent> extents;
	std::vector<std::byte> residentContent;

	fs::Timestamps timestamps;
	Confidence confidence;
	CandidateSource source;
};

} // namespace revenant::recovery
