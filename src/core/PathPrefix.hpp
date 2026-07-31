// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. One answer to "is this directory an ancestor of that path"
// (story-0609).
//
// Element-wise, never by spelling: `/mnt/data` is not an ancestor of
// `/mnt/database`, though its characters are a prefix of them. Two callers ask
// — the destination rule, deciding whether an output tree would grow around its
// source, and the mount table, deciding which mount covers a path — and getting
// it wrong in either place lets a run write where it reads.

#include <filesystem>

namespace revenant {

// Whether every element of `ancestor` starts `path`. A path is its own
// ancestor, which is what both callers want: a mount covers its own mount
// point, and a destination contains itself.
[[nodiscard]] bool
startsPath(const std::filesystem::path& ancestor, const std::filesystem::path& path);

} // namespace revenant
