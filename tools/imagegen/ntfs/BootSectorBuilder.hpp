// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <vector>

#include "imagegen/ntfs/NtfsLayout.hpp"

namespace revenant::imagegen::ntfs {

// Builds the volume's boot sector: exactly `layout.bytesPerSector` bytes that
// `revenant::fs::ntfs::parseBootSector` accepts and reads back as `layout`.
[[nodiscard]] std::vector<std::byte> buildBootSector(const NtfsLayout& layout);

} // namespace revenant::imagegen::ntfs
