// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Telling one write failure from all the others: the volume having no
// room left. Not a public interface.

#include <filesystem>

#include "revenant/core/Error.hpp"

namespace revenant::recovery {

// Which code a write failure at `path` deserves.
//
// The streams themselves will not say — an `ofstream` reports only "bad" — so
// the question goes to the filesystem at the moment of the failure. It is worth
// asking twice over: exhausted storage is the one write failure an operator can
// act on, and it is the one where every further write is known futile, so it
// stops the run instead of being counted and stepped over (story-0605).
//
// A filesystem that will not answer leaves the failure as `kIoFailure`, which
// is what it was before the question was asked.
[[nodiscard]] ErrorCode writeFailureAt(const std::filesystem::path& path);

} // namespace revenant::recovery
