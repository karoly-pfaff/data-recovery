// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. ADR-0005's destination rule, which is two rules because a source
// is one of two things. An image file is judged by path spelling — the output
// tree must not grow around the image it reads, and a destination sharing a
// volume with an image is normal practice. A device is judged by physical
// identity, because a raw device path shares no path element with anything and
// spelling therefore answers nothing at all (story-0609).

#include <filesystem>
#include <optional>

#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/core/io/DeviceIdentity.hpp"

namespace revenant::recovery {

// The device tier's verdict over two resolved identities, or nothing when the
// destination is somewhere the source does not reach. An identity that could
// not be resolved refuses the run rather than being read as "elsewhere": when
// the check cannot prove the destination is safe, it does not gamble.
[[nodiscard]] std::optional<Error>
refuseOverlap(const Result<StorageExtents>& source, const Result<StorageExtents>& destination);

// The whole rule: classify the source, then apply the tier that fits it.
[[nodiscard]] std::optional<Error>
destinationOnSource(const std::filesystem::path& destination, const std::filesystem::path& source);

} // namespace revenant::recovery
