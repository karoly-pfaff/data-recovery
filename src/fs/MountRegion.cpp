// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/MountRegion.hpp"

#include <cstddef>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::fs {

Result<std::vector<std::byte>> readMountRegion(BlockDevice& device, const MountRegion& region) {
	std::vector<std::byte> bytes(region.length, std::byte{0});
	const auto read = device.readAt(region.offset, bytes);
	if (!read.hasValue()) {
		return read.error();
	}
	if (read.value() != bytes.size()) {
		return Error{.code = ErrorCode::kNotFound};
	}
	return bytes;
}

} // namespace revenant::fs
