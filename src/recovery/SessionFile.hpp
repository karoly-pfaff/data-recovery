// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. How a file in the session directory is replaced whole-or-nothing:
// written beside its target and renamed over it. The checkpoint and the
// manifest both need it, and for the same reason — a replacement interrupted or
// refused halfway must leave the previous whole file, not a truncated one. Not
// a public interface.

#include <filesystem>
#include <span>
#include <string_view>

#include "revenant/core/Result.hpp"

namespace revenant::recovery {

// `raw` written into `directory / name`, via a pending file renamed over it.
//
// The rename is what makes the replacement atomic; the pending write is what
// runs out of room, so a destination that filled up leaves the previous file
// standing rather than a half-written one.
[[nodiscard]] Result<std::filesystem::path> replaceFile(
	const std::filesystem::path& directory,
	std::string_view name,
	std::span<const std::byte> raw);

} // namespace revenant::recovery
