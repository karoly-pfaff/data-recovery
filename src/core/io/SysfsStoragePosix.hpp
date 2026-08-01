// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal, POSIX. What sysfs says about where a block device's bytes really
// are (story-0609).

#include <cstdint>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/DeviceIdentity.hpp"

namespace revenant {

// The physical storage a block device covers, each range keyed by the `dev_t`
// of the disk it sits on: a partition's window on its disk, or a whole disk.
//
// A device built on other devices — LVM, LUKS, md — is resolved through the
// kernel's own `slaves` links to the union of what it is built from. That union
// is a superset of what the device actually occupies, and deliberately so:
// against ADR-0005's destination rule the only safe direction to err is toward
// refusing, and a mapped destination that reports itself as a disk of its own
// is how output lands on the disk being recovered.
[[nodiscard]] Result<StorageExtents> storageOfBlockDevice(std::uint64_t deviceNumber);

} // namespace revenant
