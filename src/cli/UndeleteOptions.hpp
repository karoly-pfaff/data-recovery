// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <span>
#include <string_view>

#include "revenant/core/Result.hpp"
#include "revenant/recovery/HybridRecovery.hpp"

namespace revenant::cli {

// Where a run's durable state goes when the operator does not say. The
// destination is the one directory already known to exist, to be writable by
// intent, and not to be the source (ADR-0005).
inline constexpr std::string_view kSessionDirectoryName = ".revenant";

// The command line, parsed. Every field names something `recovery/` already
// defines: the CLI carries the operator's choice of policy, never a policy of
// its own.
struct UndeleteOptions {
	std::filesystem::path source;
	std::filesystem::path destination;
	std::filesystem::path session;
	recovery::RecoveryMode mode;
};

// Parses the arguments *after* the program name:
//
//   --source <image> --destination <directory>
//   [--hybrid | --fs-only | --carve-only] [--session <directory>]
//
// Both paths are required and the mode defaults to hybrid. A second mode flag
// is refused rather than resolved: two contradictory instructions are not a
// refinement of one, and guessing which was meant is exactly the silent wrong
// thing a recovery tool must not do.
[[nodiscard]] Result<UndeleteOptions>
parseUndeleteOptions(std::span<const std::string_view> arguments);

} // namespace revenant::cli
