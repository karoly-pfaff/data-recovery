// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. The bytes a mounter needs before it can recognize a volume, read
// once for every filesystem rather than once per filesystem. Not a public
// interface.

#include <cstddef>
#include <vector>

#include "revenant/core/Result.hpp"
#include "revenant/core/io/BlockDevice.hpp"

namespace revenant::fs {

// The volume's first `length` bytes, or the reason there are none. Every
// filesystem this build reads names itself inside them.
//
// A device too short to hold them carries no filesystem at all, so that is
// `kNotFound` — the mount table's "keep looking" — rather than a truncated
// read. A device that faults keeps its own `kIoFailure`: a disk that will not
// answer is not a disk with nothing on it.
[[nodiscard]] Result<std::vector<std::byte>>
readVolumeStart(BlockDevice& device, std::size_t length);

} // namespace revenant::fs
