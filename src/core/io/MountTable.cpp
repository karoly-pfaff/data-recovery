// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/io/MountTable.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/TextNumber.hpp"

namespace revenant {

namespace {

// mountinfo's fixed fields, then a variable run of optional ones, then a lone
// "-", then the filesystem type, the source and the super options.
constexpr std::size_t kMountPointField = 4;
constexpr std::size_t kSourceAfterSeparator = 2;
constexpr std::string_view kSeparator = "-";

// The four characters that would otherwise break the field split are escaped as
// three octal digits.
constexpr std::size_t kEscapeDigits = 3;
constexpr int kOctal = 8;

// Split on one character, keeping nothing empty. Hand-rolled rather than
// `views::split`, whose C++20 form does not yield the contiguous ranges a
// `string_view` needs.
[[nodiscard]] std::vector<std::string_view> splitOn(std::string_view text, char separator) {
	std::vector<std::string_view> parts;
	for (std::size_t at = 0; at < text.size();) {
		const auto end = std::min(text.find(separator, at), text.size());
		if (end != at) {
			parts.push_back(text.substr(at, end - at));
		}
		at = end + 1;
	}
	return parts;
}

[[nodiscard]] std::optional<char> escapedAt(std::string_view field, std::size_t at) {
	if (field.at(at) != '\\' || at + kEscapeDigits >= field.size()) {
		return std::nullopt;
	}
	const auto value = numberIn(field.substr(at + 1, kEscapeDigits), kOctal);
	return value.has_value() ? std::optional{static_cast<char>(value.value())} : std::nullopt;
}

[[nodiscard]] std::string unescaped(std::string_view field) {
	std::string plain;
	for (std::size_t at = 0; at < field.size(); ++at) {
		const auto escape = escapedAt(field, at);
		plain.push_back(escape.value_or(field.at(at)));
		at += escape.has_value() ? kEscapeDigits : 0;
	}
	return plain;
}

// One mount point, and what it was mounted from.
struct Mount {
	std::filesystem::path point;
	std::string source;
};

[[nodiscard]] std::optional<Mount> mountIn(std::string_view line) {
	const auto fields = splitOn(line, ' ');
	const auto separator = std::ranges::find(fields, kSeparator);
	if (separator == fields.end() || fields.size() <= kMountPointField) {
		return std::nullopt;
	}
	const auto source = separator + kSourceAfterSeparator;
	if (source >= fields.end()) {
		return std::nullopt;
	}
	return Mount{.point = unescaped(fields.at(kMountPointField)), .source = unescaped(*source)};
}

// Element-wise, so a sibling name is not read as a prefix: `/mnt/data` does not
// cover `/mnt/database`, though its spelling does.
[[nodiscard]] bool covers(const std::filesystem::path& point, const std::filesystem::path& path) {
	const auto reach = std::ranges::mismatch(point, path);
	return reach.in1 == point.end();
}

[[nodiscard]] std::size_t depthOf(const std::filesystem::path& point) {
	return static_cast<std::size_t>(std::distance(point.begin(), point.end()));
}

[[nodiscard]] std::vector<Mount>
coveringMounts(std::string_view mountInfo, const std::filesystem::path& path) {
	std::vector<Mount> covering;
	for (const auto line : splitOn(mountInfo, '\n')) {
		auto mount = mountIn(line);
		if (mount.has_value() && covers(mount->point, path)) {
			covering.push_back(std::move(mount.value()));
		}
	}
	return covering;
}

// The deepest wins, because that is the one a path is really on — and the last
// of equally deep ones, so a mount point mounted over twice answers with the
// entry that is actually on top.
[[nodiscard]] std::optional<Mount> deepestOf(const std::vector<Mount>& covering) {
	std::optional<Mount> deepest;
	for (const auto& mount : covering) {
		if (!deepest.has_value() || depthOf(mount.point) >= depthOf(deepest->point)) {
			deepest = mount;
		}
	}
	return deepest;
}

} // namespace

std::optional<std::string>
mountSourceFor(std::string_view mountInfo, const std::filesystem::path& path) {
	auto deepest = deepestOf(coveringMounts(mountInfo, path));
	return deepest.has_value() ? std::optional{std::move(deepest->source)} : std::nullopt;
}

} // namespace revenant
