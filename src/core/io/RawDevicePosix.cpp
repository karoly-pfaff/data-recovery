// SPDX-License-Identifier: GPL-3.0-or-later
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <filesystem>

#include "core/io/RawDeviceNative.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/ReadRange.hpp"

namespace revenant {

namespace {

// What every partition table's own arithmetic assumes, and what a device that
// will not answer BLKSSZBGET is overwhelmingly likely to be.
constexpr std::uint32_t kAssumedSectorSize = 512;

// The two requests, restated in the type ioctl(2) takes them in: the kernel
// headers spell them with a macro whose type varies, and passing that straight
// through is a sign-conversion warning on some glibc versions and not others.
constexpr unsigned long kSizeRequest = BLKGETSIZE64;
constexpr unsigned long kSectorSizeRequest = BLKSSZGET;

// ioctl(2) is variadic; these two are the documented way to ask a block device
// its size and its logical sector size, and there is no non-vararg form of
// either.
// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
[[nodiscard]] Result<std::uint64_t> queryDeviceSize(int fd) {
	std::uint64_t bytes = 0;
	if (::ioctl(fd, kSizeRequest, &bytes) != 0) {
		const int savedErrno = errno;
		::close(fd);
		return Error{.code = ErrorCode::kIoFailure, .offset = 0, .osCode = savedErrno};
	}
	return bytes;
}

// A device that will not say falls back to 512 rather than failing the open: a
// wrong-but-conventional sector size still reads, and refusing would lose a
// device that is otherwise perfectly readable.
[[nodiscard]] std::uint32_t queryLogicalSectorSize(int fd) {
	int sectorSize = 0;
	if (::ioctl(fd, kSectorSizeRequest, &sectorSize) != 0 || sectorSize <= 0) {
		return kAssumedSectorSize;
	}
	return static_cast<std::uint32_t>(sectorSize);
}

// NOLINTEND(cppcoreguidelines-pro-type-vararg)

[[nodiscard]] Result<OpenedRawDevice> measure(std::intptr_t handle) {
	const int fd = static_cast<int>(handle);
	return queryDeviceSize(fd).map([fd, handle](std::uint64_t bytes) {
		return OpenedRawDevice{
			.nativeHandle = handle,
			.sizeInBytes = bytes,
			.sectorSize = queryLogicalSectorSize(fd)};
	});
}

} // namespace

Result<OpenedRawDevice> acquireRawDevice(const std::filesystem::path& devicePath) {
	return openReadOnly(devicePath).andThen(measure);
}

} // namespace revenant
