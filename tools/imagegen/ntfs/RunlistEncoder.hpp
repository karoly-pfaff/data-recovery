// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "revenant/fs/ntfs/Runlist.hpp"

namespace revenant::imagegen::ntfs {

// Encodes cluster runs as NTFS data runs — the exact inverse of the production
// `decodeRunlist`, which is how it is specified and tested. Field widths are
// the narrowest that hold each value, the way a real NTFS driver writes them,
// so a fixture built here exercises the decoder's width handling rather than
// one fixed shape.
[[nodiscard]] std::vector<std::byte>
encodeRunlist(std::span<const revenant::fs::ntfs::DataRun> runs);

} // namespace revenant::imagegen::ntfs
