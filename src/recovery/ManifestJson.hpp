// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal. Just enough JSON to emit a manifest: six value shapes and no
// parsing side. A dependency and its supply-chain review (story-0008) would
// cost more than the escaping it replaces. Not a public interface.

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace revenant::recovery::json {

// `text` as a JSON string: quoted, with quotes, backslashes and control
// characters escaped, so a hostile filename cannot break the document.
[[nodiscard]] std::string quotedText(std::string_view text);

// A path as a JSON string, encoded as UTF-8 rather than in whatever narrow
// encoding the platform happens to use.
[[nodiscard]] std::string quotedPath(const std::filesystem::path& path);

[[nodiscard]] std::string member(std::string_view name, std::string_view text);

[[nodiscard]] std::string member(std::string_view name, std::uint64_t value);

// A member whose value is already JSON — an object, an array, a number.
[[nodiscard]] std::string rawMember(std::string_view name, std::string_view value);

[[nodiscard]] std::string object(std::span<const std::string> members);

[[nodiscard]] std::string array(std::span<const std::string> items);

} // namespace revenant::recovery::json
