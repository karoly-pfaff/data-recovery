// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/TempDir.hpp"

#include <atomic>
#include <filesystem>
#include <string>
#include <system_error>

namespace revenant::testing {

namespace {

std::filesystem::path uniqueTempDirectory() {
	static std::atomic<unsigned> counter{0};
	const auto name = "revenant-session-" + std::to_string(counter.fetch_add(1));
	return std::filesystem::temp_directory_path() / name;
}

} // namespace

TempDir::TempDir() : path_(uniqueTempDirectory()) {
	std::error_code ignored;
	std::filesystem::remove_all(path_, ignored);
	std::filesystem::create_directories(path_, ignored);
}

// The error_code overloads do not throw, but the path operations they are
// handed can still allocate; a destructor may not let that escape.
TempDir::~TempDir() {
	try {
		std::error_code ignored;
		std::filesystem::remove_all(path_, ignored);
	} catch (...) { // NOLINT(bugprone-empty-catch) - nothing to report from a destructor.
	}
}

} // namespace revenant::testing
