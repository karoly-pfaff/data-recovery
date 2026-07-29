// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/VolumeReader.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::fs {

VolumeReader::VolumeReader(BlockDevice& device) noexcept : device_(&device) {}

Result<std::size_t> VolumeReader::read(std::uint64_t offset, std::span<std::byte> buffer) const {
	const auto got = device_->readAt(offset, buffer);
	if (got.hasValue() && got.value() != buffer.size()) {
		return Error{.code = ErrorCode::kOutOfRange, .offset = offset};
	}
	return got;
}

} // namespace revenant::fs
