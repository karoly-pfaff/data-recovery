// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "revenant/core/Result.hpp"

namespace revenant::cli {

// What `--list-partitions` prints for `source`: a heading naming the scheme and
// one line per partition — number, offset, length, and the label that lets an
// operator recognize it.
//
// A source with no readable table is *not* a failure: it is a single volume, and
// saying so is the answer to the question. Only a source that cannot be opened
// at all is a typed error.
//
// Read-only throughout (ADR-0005): this opens the source, reads its table, and
// returns. Nothing is written and no destination is needed.
[[nodiscard]] Result<std::vector<std::string>>
describePartitions(const std::filesystem::path& source);

} // namespace revenant::cli
