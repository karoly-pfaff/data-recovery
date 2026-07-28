// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <vector>

#include "imagegen/fat/Fat32Layout.hpp"

namespace revenant::imagegen::fat {

// Writes the fixture's three directories into `image`: the root, `photos`
// under it, and the deleted `gone` — whose entries are what makes the file
// inside it an orphan rather than merely deleted.
void putDirectories(std::vector<std::byte>& image, const Fat32Layout& layout);

} // namespace revenant::imagegen::fat
