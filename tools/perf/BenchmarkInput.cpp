// SPDX-License-Identifier: GPL-3.0-or-later
#include "perf/BenchmarkInput.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "imagegen/FixtureBytes.hpp"
#include "imagegen/disk/DiskImageBuilder.hpp"
#include "imagegen/ntfs/FixtureFiles.hpp"
#include "imagegen/ntfs/NtfsImageBuilder.hpp"

namespace revenant::perf {

namespace {

constexpr std::size_t kPlantedJpegBytes = std::size_t{8} << 10U;

// Filler that no signature matches, so every planted header is a header the
// scanner found rather than one it tripped over. `fixtureContent` is the same
// non-repeating generator every image builder uses.
[[nodiscard]] std::vector<std::byte> filler() {
	return imagegen::fixtureContent(kScanImageBytes, std::byte{0x5A});
}

[[nodiscard]] std::vector<std::byte> buildScanImage() {
	std::vector<std::byte> image = filler();
	const auto planted = imagegen::ntfs::fixtureJpeg(kPlantedJpegBytes);
	for (std::size_t at = 0; at + planted.size() < image.size(); at += kHeaderEveryBytes) {
		std::ranges::copy(planted, image.begin() + static_cast<std::ptrdiff_t>(at));
	}
	return image;
}

// A JPEG cut off part-way through its entropy-coded scan: the carver has to walk
// it before it can say no, which is the expensive half of validation.
[[nodiscard]] std::vector<std::byte> buildTruncatedJpeg() {
	auto whole = imagegen::ntfs::fixtureJpeg(kPlantedJpegBytes);
	whole.resize(whole.size() / 2);
	return whole;
}

} // namespace

const std::vector<std::byte>& scanImage() {
	static const std::vector<std::byte> kImage = buildScanImage();
	return kImage;
}

const std::vector<std::byte>& ntfsVolume() {
	static const std::vector<std::byte> kVolume = imagegen::ntfs::buildNtfsImage();
	return kVolume;
}

const std::vector<std::byte>& wholeDisk() {
	static const std::vector<std::byte> kDisk = imagegen::disk::buildMbrDiskImage().bytes;
	return kDisk;
}

const std::vector<std::byte>& validJpeg() {
	static const std::vector<std::byte> kJpeg = imagegen::ntfs::fixtureJpeg(kPlantedJpegBytes);
	return kJpeg;
}

const std::vector<std::byte>& truncatedJpeg() {
	static const std::vector<std::byte> kJpeg = buildTruncatedJpeg();
	return kJpeg;
}

} // namespace revenant::perf
