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

} // namespace

bool runCli(CliFrontend frontend, std::vector<std::string> arguments) {
	const auto argv = pointersTo(arguments);
	return frontend(std::span<char* const>{argv});
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

bool CliFixture::run(CliFrontend frontend, const std::vector<std::string>& flags) const {
	std::vector<std::string> arguments{
		"revenant",
		"--source",
		source().string(),
		"--destination",
		destination().string()};
	arguments.insert(arguments.end(), flags.begin(), flags.end());
	return runCli(frontend, arguments);
}

std::filesystem::path CliFixture::recovered(const std::string& relative) const {
	return destination() / std::filesystem::path{relative};
}

} // namespace revenant::testing
