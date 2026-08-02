// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cli/RunOutcome.hpp"
#include "support/TempDir.hpp"
#include "support/TempFile.hpp"

namespace revenant::testing {

// A command-line frontend, as `main` sees it.
using CliFrontend = cli::RunOutcome (*)(std::span<char* const>);

// Runs `frontend` over `arguments` — program name included — the way a shell
// hands them to `main`. True means the run finished; a test that cares *how* it
// stopped asks `outcomeOfCli` instead.
[[nodiscard]] bool runCli(CliFrontend frontend, std::vector<std::string> arguments);

// The same run, reported as the exit status it would produce.
[[nodiscard]] cli::RunOutcome
outcomeOfCli(CliFrontend frontend, std::vector<std::string> arguments);

// Whether anything under `directory` carries `extension` (".jpg"). False when
// the directory is not there at all, which is what "nothing was carved" looks
// like on disk.
[[nodiscard]] bool
holdsFileOfType(const std::filesystem::path& directory, std::string_view extension);

// The story-0118 fixture image and a destination to recover it into, plus the
// command line that points a frontend at the two.
class CliFixture {
public:
	CliFixture();

	[[nodiscard]] bool run(CliFrontend frontend, const std::vector<std::string>& flags) const;

	[[nodiscard]] cli::RunOutcome
	outcomeOf(CliFrontend frontend, const std::vector<std::string>& flags) const;

	[[nodiscard]] std::filesystem::path recovered(const std::string& relative) const;

	[[nodiscard]] const std::filesystem::path& source() const noexcept {
		return image_.path();
	}

	[[nodiscard]] const std::filesystem::path& destination() const noexcept {
		return output_.path();
	}

private:
	TempFile image_;
	TempDir output_;
};

} // namespace revenant::testing
