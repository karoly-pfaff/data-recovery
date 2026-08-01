// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/io/SysfsStoragePosix.hpp"

#include <sys/sysmacros.h>

#include <cstdint>
#include <string>

#include "core/io/SysfsWalk.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/DeviceIdentity.hpp"

namespace revenant {

namespace {

// The kernel's flat index of every block device, by the name a `dev_t` spells.
constexpr const char* kSysfsBlockRoot = "/sys/dev/block";

} // namespace

Result<StorageExtents> storageOfBlockDevice(std::uint64_t deviceNumber) {
	const std::string name =
		std::to_string(major(deviceNumber)) + ":" + std::to_string(minor(deviceNumber));
	return storageUnderSysfs(kSysfsBlockRoot, name);
}

} // namespace revenant
