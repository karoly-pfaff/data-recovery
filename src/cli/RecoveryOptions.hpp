// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cli/RecoveryRun.hpp"
#include "revenant/core/Error.hpp"
#include "revenant/core/Result.hpp"
#include "revenant/recovery/HybridRecovery.hpp"

namespace revenant::cli {

// Where a run's durable state goes when the operator does not say. The
// destination is the one directory already known to exist, to be writable by
// intent, and not to be the source (ADR-0005).
inline constexpr std::string_view kSessionDirectoryName = ".revenant";

// The arguments a grammar has not read yet. Flags are consumed from the front,
// so every step hands the next one what is left rather than an index into a
// shared sequence.
using Arguments = std::span<const std::string_view>;

// The options as a command line states them, before defaults are applied. The
// mode is optional here and only here: it has no value until a flag names one,
// which is how a second one is caught.
struct OptionDraft {
	std::filesystem::path source;
	std::filesystem::path destination;
	std::filesystem::path session;
	std::optional<recovery::RecoveryMode> mode;
	std::optional<Delivery> delivery;
	std::vector<std::string> formats;
};

// A frontend's own flags, handed the arguments starting at the one to look at.
// Returns what is left after consuming it — or a usage error, which is also the
// answer for a flag no grammar owns.
using ExtraFlags = Result<Arguments> (*)(OptionDraft&, Arguments);

// The one refusal a command line can earn. Which flag was wrong is answered by
// printing the grammar, not by naming an error: the operator needs the shape of
// the command, not the name of a code.
[[nodiscard]] Error usageError();

// The value after `arguments.front()`, and what is left after it.
struct FlagValue {
	std::string_view value;
	Arguments rest;
};

[[nodiscard]] Result<FlagValue> valueAfterFlag(Arguments arguments);

// Reads the flags every recovery frontend shares — `--source`,
// `--destination`, `--session` — and defers everything else to `extra`. Both
// paths are required and a run always ends up with a session directory;
// `defaultMode` is what the frontend recovers in when no flag says otherwise.
[[nodiscard]] Result<RunRequest>
readRecoveryOptions(Arguments arguments, ExtraFlags extra, recovery::RecoveryMode defaultMode);

} // namespace revenant::cli
