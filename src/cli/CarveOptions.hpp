// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cli/RecoveryOptions.hpp"
#include "cli/RecoveryRun.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::cli {

// Parses `revenant-carve`'s arguments — everything after the program name:
//
//   --source <image> --destination <directory>
//   [--formats <ext,ext,…>] [--session <directory>]
//
// There is no mode flag: carving is the only thing this frontend does. An
// allowlist entry no carver answers to is refused rather than ignored, because
// the allowlist is applied at registration — obeying a misspelt format would
// produce a scan that searched for nothing and reported success.
[[nodiscard]] Result<RunRequest> parseCarveOptions(Arguments arguments);

} // namespace revenant::cli
