// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagegen/CarveCorpus.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

#include "imagegen/FixtureBytes.hpp"
#include "imagegen/FixtureJpeg.hpp"
#include "imagegen/ImageFile.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::imagegen {

namespace {

// The eight bytes every PNG opens with. Followed here by filler rather than by
// an IHDR chunk, which is what makes this half of the unit a rejection. Spelled
// as one literal rather than as a byte table: this is a fixture stating what it
// plants, not a parser's copy of the carver's own table.
constexpr std::string_view kPngSignature{"\x89PNG\r\n\x1A\n", 8};

// Distinguishes the filler from the JPEG entropy beside it; any seed works, so
// long as it is fixed.
constexpr std::byte kFillerSeed{0x11};

void append(std::vector<std::byte>& into, std::span<const std::byte> bytes) {
	into.insert(into.end(), bytes.begin(), bytes.end());
}

// A PNG signature with filler behind it: a header a scan must look at and
// cannot carve. The filler counts up, so no other format's magic occurs in it.
[[nodiscard]] std::vector<std::byte> rejectableBlock() {
	std::vector<std::byte> block;
	append(block, std::as_bytes(std::span{kPngSignature}));
	append(block, fixtureContent(kCorpusRejectBytes - kPngSignature.size(), kFillerSeed));
	return block;
}

[[nodiscard]] std::vector<std::byte> corpusUnit() {
	auto unit = fixtureJpeg(kCorpusJpegBytes);
	append(unit, rejectableBlock());
	return unit;
}

} // namespace

std::vector<std::byte> buildCarveCorpus(std::size_t sizeBytes) {
	const auto unit = corpusUnit();
	const std::span<const std::byte> whole{unit};
	std::vector<std::byte> corpus;
	corpus.reserve(sizeBytes);
	while (corpus.size() < sizeBytes) {
		append(corpus, whole.first(std::min(whole.size(), sizeBytes - corpus.size())));
	}
	return corpus;
}

Result<std::uint64_t> writeCarveCorpus(const std::filesystem::path& path, std::uint64_t sizeBytes) {
	return writeImageBytes(path, buildCarveCorpus(static_cast<std::size_t>(sizeBytes)));
}

} // namespace revenant::imagegen
