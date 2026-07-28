// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/FixtureContent.hpp"

#include <bit>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
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

std::string readFileText(const std::filesystem::path& path) {
	std::ifstream stream{path, std::ios::binary};
	return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
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
