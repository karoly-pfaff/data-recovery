// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/io/DeviceIdentity.hpp"

#include <algorithm>
#include <span>

namespace revenant {

namespace {

// Two ranges of one disk that share at least one byte. Each range is compared
// by the distance between the two starts rather than by computing its end, so
// a range reaching the last addressable byte of a disk cannot wrap the
// arithmetic into reporting no overlap — which is the one wrong answer here
// that would fail open.
[[nodiscard]] bool sharesBytes(const StorageExtent& read, const StorageExtent& written) {
	if (read.disk != written.disk) {
		return false;
	}
	return read.offsetBytes <= written.offsetBytes
			   ? written.offsetBytes - read.offsetBytes < read.lengthBytes
			   : read.offsetBytes - written.offsetBytes < written.lengthBytes;
}

} // namespace

bool overlaps(std::span<const StorageExtent> source, std::span<const StorageExtent> destination) {
	return std::ranges::any_of(source, [destination](const StorageExtent& read) {
		return std::ranges::any_of(destination, [&read](const StorageExtent& written) {
			return sharesBytes(read, written);
		});
	});
}

} // namespace revenant
