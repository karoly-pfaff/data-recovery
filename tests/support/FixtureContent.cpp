// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/FixtureContent.hpp"

#include <bit>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "imagegen/ntfs/FixtureFiles.hpp"
#include "imagegen/ntfs/NtfsLayout.hpp"

namespace revenant::testing {

std::vector<std::byte> readFileBytes(const std::filesystem::path& path) {
	std::ifstream stream{path, std::ios::binary};
	std::vector<std::byte> bytes;
	for (auto value = stream.get(); value != std::char_traits<char>::eof(); value = stream.get()) {
		bytes.push_back(std::bit_cast<std::byte>(static_cast<char>(value)));
	}
	return bytes;
}

// Reads through the buffer rather than through a pair of `istreambuf_iterator`s.
// The iterator form is what GCC inlines `sbumpc` into at `-O2`, and it then
// reports a potential null dereference of `gptr()` inside libstdc++ — a
// diagnostic our code cannot answer, on a path an `ifstream`'s buffer cannot
// take. Pumping the buffer asks the same question of one out-of-line operator.
std::string readFileText(const std::filesystem::path& path) {
	const std::ifstream stream{path, std::ios::binary};
	std::ostringstream text;
	text << stream.rdbuf();
	return std::move(text).str();
}

std::vector<std::byte> fixtureContentNamed(std::string_view name) {
	for (auto& file : imagegen::ntfs::fixtureFiles(imagegen::ntfs::makeLayout())) {
		if (file.name == name) {
			return std::move(file.content);
		}
	}
	return {};
}

} // namespace revenant::testing
