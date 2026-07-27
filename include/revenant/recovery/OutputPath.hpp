// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <filesystem>
#include <string_view>

#include "revenant/core/Result.hpp"

namespace revenant::recovery {

// ADR-0009 path confinement: every cleaned segment of a sanitized name is
// bounded to this many bytes, and a name may not decompose into more than
// this many segments — both enforced before any allocation is sized by the
// (untrusted) name.
inline constexpr std::size_t kMaxSegmentBytes = 240;
inline constexpr std::size_t kMaxSegments = 64;

// ADR-0009 path confinement — the single choke point every recovered-file
// output path passes through before any write; there is no other way to
// derive one. `relativeName` is an untrusted, decoded UTF-8 name straight
// off a filesystem or carved structure (it may contain directory
// components, e.g. "docs/report.txt"). Returns the safe path inside
// `destinationRoot`, or `kInvalidArgument` if nothing safe survives the
// name (traversal, an absolute/drive-rooted form, forbidden bytes, or a
// segment/count bound violation) — every rejection reason reports this one
// code, since they all mean the same thing to the caller: this name cannot
// be used to write anywhere. `destinationRoot` is expected to be absolute.
[[nodiscard]] Result<std::filesystem::path>
sanitizeOutputPath(const std::filesystem::path& destinationRoot, std::string_view relativeName);

} // namespace revenant::recovery
