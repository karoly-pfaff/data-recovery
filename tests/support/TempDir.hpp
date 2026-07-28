// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>

namespace revenant::testing {

// RAII session directory: a unique, empty directory under the system temp
// directory, removed with everything in it on destruction.
class TempDir {
public:
	TempDir();
	~TempDir();
	TempDir(const TempDir&) = delete;
	TempDir& operator=(const TempDir&) = delete;
	TempDir(TempDir&&) = delete;
	TempDir& operator=(TempDir&&) = delete;

	[[nodiscard]] const std::filesystem::path& path() const noexcept {
		return path_;
	}

private:
	std::filesystem::path path_;
};

} // namespace revenant::testing
