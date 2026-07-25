// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

namespace revenant::testing {

// RAII temp image: writes `content` to a unique file under the system temp
// directory on construction, removes it on destruction.
class TempFile {
public:
    explicit TempFile(const std::vector<std::byte>& content);
    ~TempFile();
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&&) = delete;
    TempFile& operator=(TempFile&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

} // namespace revenant::testing
