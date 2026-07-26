// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/InMemoryDevice.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::testing {

InMemoryDevice::InMemoryDevice(std::vector<std::byte> data, std::uint32_t sectorSize)
	: data_(std::move(data)), sectorSize_(sectorSize) {}

std::uint64_t InMemoryDevice::sizeInBytes() const {
	return data_.size();
}

std::uint32_t InMemoryDevice::sectorSize() const {
	return sectorSize_;
}

Result<std::size_t> InMemoryDevice::readAt(std::uint64_t offset, std::span<std::byte> buffer) {
	if (buffer.size() > std::numeric_limits<std::uint64_t>::max() - offset) {
		return Error{.code = ErrorCode::kOverflow, .offset = offset};
	}
	if (offset >= data_.size()) {
		return std::size_t{0};
	}
	const std::size_t available =
		std::min(buffer.size(), data_.size() - static_cast<std::size_t>(offset));
	std::copy_n(data_.begin() + static_cast<std::ptrdiff_t>(offset), available, buffer.begin());
	return available;
}

} // namespace revenant::testing
