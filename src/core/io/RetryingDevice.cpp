// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/core/io/RetryingDevice.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BadRange.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant {

RetryingDevice::RetryingDevice(BlockDevice& source, const RetryPolicy& policy) noexcept
	: source_(source), policy_(policy) {}

std::uint64_t RetryingDevice::sizeInBytes() const {
	return source_.sizeInBytes();
}

std::uint32_t RetryingDevice::sectorSize() const {
	return source_.sectorSize();
}

std::span<const BadRange> RetryingDevice::badRanges() const noexcept {
	return bad_;
}

Result<std::size_t> RetryingDevice::readAt(std::uint64_t offset, std::span<std::byte> buffer) {
	const auto whole = attemptRead(offset, buffer);
	if (whole.hasValue()) {
		return whole;
	}
	return readSectorwise(offset, buffer);
}

void RetryingDevice::waitBetweenAttempts() const {
	if (policy_.pause.count() > 0) {
		std::this_thread::sleep_for(policy_.pause);
	}
}

Result<std::size_t> RetryingDevice::attemptRead(std::uint64_t offset, std::span<std::byte> buffer) {
	Result<std::size_t> read = source_.readAt(offset, buffer);
	for (unsigned attempt = 1; attempt < policy_.attempts && !read.hasValue(); ++attempt) {
		waitBetweenAttempts();
		read = source_.readAt(offset, buffer);
	}
	return read;
}

// What could not be read whole, read one sector at a time. A zero step is the
// end of the device rather than a fault: a sector that faults is filled and
// counted, so it always advances.
std::size_t RetryingDevice::readSectorwise(std::uint64_t offset, std::span<std::byte> buffer) {
	std::size_t done = 0;
	while (done < buffer.size()) {
		const auto step = readOneSector(offset + done, buffer.subspan(done));
		if (step == 0) {
			return done;
		}
		done += step;
	}
	return done;
}

std::size_t RetryingDevice::readOneSector(std::uint64_t offset, std::span<std::byte> buffer) {
	const auto count = std::min<std::size_t>(buffer.size(), source_.sectorSize());
	const std::span<std::byte> sector = buffer.first(count);
	const auto read = attemptRead(offset, sector);
	if (read.hasValue()) {
		return read.value();
	}
	std::ranges::fill(sector, std::byte{0});
	recordBad(BadRange{.offsetBytes = offset, .lengthBytes = count});
	return count;
}

void RetryingDevice::recordBad(const BadRange& range) {
	if (!bad_.empty() && bad_.back().offsetBytes + bad_.back().lengthBytes == range.offsetBytes) {
		bad_.back().lengthBytes += range.lengthBytes;
		return;
	}
	bad_.push_back(range);
}

} // namespace revenant
