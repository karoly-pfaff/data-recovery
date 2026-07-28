// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/MountRegion.hpp"

#include <cstddef>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::fs {

Result<std::vector<std::byte>> readVolumeStart(BlockDevice& device, std::size_t length) {
	std::vector<std::byte> region(length, std::byte{0});
	const auto read = device.readAt(0, region);
	if (!read.hasValue()) {
		return read.error();
	}
	if (read.value() != region.size()) {
		return Error{.code = ErrorCode::kNotFound};
	}
	return region;
}

} // namespace revenant::fs
