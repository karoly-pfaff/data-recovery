// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/FaultyDevice.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::testing {

namespace {

[[nodiscard]] bool overlaps(const Fault& fault, std::uint64_t offset, std::size_t length) {
	return offset < fault.offsetBytes + fault.lengthBytes && fault.offsetBytes < offset + length;
}

// Whether this read is refused. A transient fault spends one of its refusals
// each time it is met, and reads normally once they are gone.
[[nodiscard]] bool refuses(Fault& fault) {
	if (fault.permanent) {
		return true;
	}
	if (fault.refusals == 0) {
		return false;
	}
	--fault.refusals;
	return true;
}

} // namespace

FaultyDevice::FaultyDevice(
	std::vector<std::byte> data,
	std::uint32_t sectorSize,
	std::vector<Fault> faults)
	: FaultyDevice(std::move(data), sectorSize, std::move(faults), WhenLost::kNever) {}

FaultyDevice::FaultyDevice(
	std::vector<std::byte> data,
	std::uint32_t sectorSize,
	std::vector<Fault> faults,
	WhenLost lost)
	: data_(std::move(data)), sectorSize_(sectorSize), faults_(std::move(faults)), lost_(lost) {}

std::uint64_t FaultyDevice::sizeInBytes() const {
	return data_.size();
}

std::uint32_t FaultyDevice::sectorSize() const {
	return sectorSize_;
}

Fault* FaultyDevice::faultFor(std::uint64_t offset, std::size_t length) {
	for (Fault& fault : faults_) {
		if (overlaps(fault, offset, length)) {
			return &fault;
		}
	}
	return nullptr;
}

std::size_t FaultyDevice::availableAt(std::uint64_t offset, std::size_t wanted) const {
	if (offset >= data_.size()) {
		return 0;
	}
	return std::min(wanted, data_.size() - static_cast<std::size_t>(offset));
}

std::size_t
FaultyDevice::copyOut(std::uint64_t offset, std::span<std::byte> buffer, std::size_t count) {
	if (count == 0) {
		return 0;
	}
	std::copy_n(data_.begin() + static_cast<std::ptrdiff_t>(offset), count, buffer.begin());
	return count;
}

Result<std::size_t> FaultyDevice::readAt(std::uint64_t offset, std::span<std::byte> buffer) {
	++reads_;
	if (gone_) {
		return Error{.code = ErrorCode::kIoFailure, .offset = offset};
	}
	const auto available = availableAt(offset, buffer.size());
	Fault* fault = faultFor(offset, available);
	if (fault != nullptr && refuses(*fault)) {
		gone_ = lost_ == WhenLost::kAfterTheFirstFault;
		return Error{.code = ErrorCode::kIoFailure, .offset = offset};
	}
	return copyOut(offset, buffer, available);
}

} // namespace revenant::testing
