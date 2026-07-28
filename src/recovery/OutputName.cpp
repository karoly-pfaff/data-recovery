// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/recovery/OutputName.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

#include "revenant/recovery/Candidate.hpp"

namespace revenant::recovery {

namespace {

// Wide enough that a device full of carved files never wraps into ambiguity,
// and narrow enough to stay readable.
constexpr int kOrdinalDigits = 8;

// A carver's extension arrives as data, so it may not become a directory name
// unchecked: anything that is not a plain lowercase-alphanumeric tag is not a
// format this build produced, and lands in the unknown bucket rather than
// steering the path.
[[nodiscard]] bool isPlainTag(std::string_view extension) {
	return !extension.empty() && extension.size() <= 8 &&
		   std::ranges::all_of(extension, [](char letter) {
			   return (letter >= 'a' && letter <= 'z') || (letter >= '0' && letter <= '9');
		   });
}

[[nodiscard]] std::string bucketOf(const Candidate& candidate) {
	return isPlainTag(candidate.name) ? candidate.name : std::string{kUnknownExtension};
}

[[nodiscard]] std::string carvedName(const Candidate& candidate, std::uint64_t ordinal) {
	const auto bucket = bucketOf(candidate);
	std::ostringstream name;
	name << kCarvedRoot << '/' << bucket << "/f" << std::setfill('0') << std::setw(kOrdinalDigits)
		 << ordinal << '.' << bucket;
	return name.str();
}

} // namespace

std::string outputNameFor(const Candidate& candidate, std::uint64_t ordinal) {
	if (candidate.source == CandidateSource::kFilesystem) {
		return candidate.name;
	}
	return carvedName(candidate, ordinal);
}

} // namespace revenant::recovery
