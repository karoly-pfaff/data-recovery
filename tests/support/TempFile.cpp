// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/TempFile.hpp"

#include <atomic>
#include <bit>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>
#include <vector>

namespace revenant::testing {

namespace {

std::filesystem::path uniqueTempPath() {
    static std::atomic<unsigned> counter{0};
    const auto name = "revenant-test-" + std::to_string(counter.fetch_add(1)) + ".img";
    return std::filesystem::temp_directory_path() / name;
}

} // namespace

TempFile::TempFile(const std::vector<std::byte>& content) : path_(uniqueTempPath()) {
    std::ofstream stream{path_, std::ios::binary | std::ios::trunc};
    for (const std::byte b : content) {
        stream.put(std::bit_cast<char>(b));
    }
}

TempFile::~TempFile() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
}

} // namespace revenant::testing
