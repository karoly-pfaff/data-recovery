// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/CliFixture.hpp"

#include <algorithm>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "imagegen/ntfs/NtfsImageBuilder.hpp"

namespace revenant::testing {

namespace {

[[nodiscard]] std::vector<char*> pointersTo(std::vector<std::string>& arguments) {
	std::vector<char*> argv;
	argv.reserve(arguments.size());
	for (std::string& argument : arguments) {
		argv.push_back(argument.data());
	}
	return argv;
}

// One place builds the command line the two entry points share, so a test that
// asks "did it finish" and one that asks "how did it stop" cannot disagree
// about what was run.
[[nodiscard]] cli::RunOutcome invoke(CliFrontend frontend, std::vector<std::string>& arguments) {
	const auto argv = pointersTo(arguments);
	return frontend(std::span<char* const>{argv});
}

} // namespace

cli::RunOutcome outcomeOfCli(CliFrontend frontend, std::vector<std::string> arguments) {
	return invoke(frontend, arguments);
}

bool runCli(CliFrontend frontend, std::vector<std::string> arguments) {
	return invoke(frontend, arguments) == cli::RunOutcome::kFinished;
}

bool holdsFileOfType(const std::filesystem::path& directory, std::string_view extension) {
	if (!std::filesystem::is_directory(directory)) {
		return false;
	}
	const std::filesystem::recursive_directory_iterator entries{directory};
	return std::ranges::any_of(entries, [extension](const std::filesystem::directory_entry& entry) {
		return entry.path().extension() == extension;
	});
}

CliFixture::CliFixture() : image_(imagegen::ntfs::buildNtfsImage()) {}

namespace {

[[nodiscard]] std::vector<std::string> commandLine(
	const std::filesystem::path& source,
	const std::filesystem::path& destination,
	const std::vector<std::string>& flags) {
	std::vector<std::string>
		arguments{"revenant", "--source", source.string(), "--destination", destination.string()};
	arguments.insert(arguments.end(), flags.begin(), flags.end());
	return arguments;
}

} // namespace

bool CliFixture::run(CliFrontend frontend, const std::vector<std::string>& flags) const {
	return runCli(frontend, commandLine(source(), destination(), flags));
}

cli::RunOutcome
CliFixture::outcomeOf(CliFrontend frontend, const std::vector<std::string>& flags) const {
	return outcomeOfCli(frontend, commandLine(source(), destination(), flags));
}

std::filesystem::path CliFixture::recovered(const std::string& relative) const {
	return destination() / std::filesystem::path{relative};
}

} // namespace revenant::testing
