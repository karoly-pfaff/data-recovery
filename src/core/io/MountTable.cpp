// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/io/MountTable.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/TextNumber.hpp"

namespace revenant {

namespace {

// mountinfo's fixed fields, then a variable run of optional ones, then a lone
// "-", then the filesystem type, the source and the super options.
constexpr std::size_t kDeviceField = 2;
constexpr std::size_t kMountPointField = 4;
constexpr std::size_t kTypeAfterSeparator = 1;
constexpr std::size_t kSourceAfterSeparator = 2;
constexpr std::string_view kSeparator = "-";

// The four characters that would otherwise break the field split are escaped as
// three octal digits.
constexpr std::size_t kEscapeDigits = 3;
constexpr int kOctal = 8;
constexpr char kLowestOctalDigit = '0';
constexpr char kHighestOctalDigit = '7';

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

[[nodiscard]] bool isOctalRun(std::string_view digits) {
	return digits.size() == kEscapeDigits && std::ranges::all_of(digits, [](char digit) {
			   return digit >= kLowestOctalDigit && digit <= kHighestOctalDigit;
		   });
}

// All three digits are required. A partial escape is not an escape, and reading
// one as though it were would silently eat the two characters after it.
[[nodiscard]] std::optional<char> escapedAt(std::string_view field, std::size_t at) {
	if (field.at(at) != '\\' || at + kEscapeDigits >= field.size()) {
		return std::nullopt;
	}
	const auto digits = field.substr(at + 1, kEscapeDigits);
	if (!isOctalRun(digits)) {
		return std::nullopt;
	}
	return static_cast<char>(numberIn(digits, kOctal).value_or(0));
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

// One mount point, what the filesystem on it numbers itself, and what it was
// mounted from.
struct Mount {
	std::filesystem::path point;
	std::uint64_t fsDevice;
	MountSource from;
};

// "major:minor", as one number, so a mountinfo line can be matched against a
// `st_dev`. Zero when the field will not parse — no filesystem reports zero.
[[nodiscard]] std::uint64_t deviceNumberIn(std::string_view field) {
	const auto colon = field.find(':');
	if (colon == std::string_view::npos) {
		return 0;
	}
	const auto high = numberIn(field.substr(0, colon)).value_or(0);
	const auto low = numberIn(field.substr(colon + 1)).value_or(0);
	return (high << 32U) | low;
}

// Where the "-" sits, when the line has one and enough fields after it. The
// distance is measured before the iterator is advanced: forming an iterator
// past the end to test it is undefined, which would make the guard the fault.
[[nodiscard]] std::optional<std::size_t> separatorIn(std::span<const std::string_view> fields) {
	const auto separator = std::ranges::find(fields, kSeparator);
	const auto at = static_cast<std::size_t>(std::distance(fields.begin(), separator));
	if (separator == fields.end() || fields.size() <= at + kSourceAfterSeparator) {
		return std::nullopt;
	}
	return at;
}

[[nodiscard]] std::optional<Mount> mountIn(std::string_view line) {
	const auto fields = splitOn(line, ' ');
	const auto separator = separatorIn(fields);
	if (!separator.has_value() || fields.size() <= kMountPointField) {
		return std::nullopt;
	}
	return Mount{
		.point = unescaped(fields.at(kMountPointField)),
		.fsDevice = deviceNumberIn(fields.at(kDeviceField)),
		.from = MountSource{
			.type = unescaped(fields.at(separator.value() + kTypeAfterSeparator)),
			.source = unescaped(fields.at(separator.value() + kSourceAfterSeparator))}};
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
[[nodiscard]] std::optional<Mount> deepestOf(std::span<const Mount> covering) {
	std::optional<Mount> deepest;
	for (const auto& mount : covering) {
		if (!deepest.has_value() || depthOf(mount.point) >= depthOf(deepest->point)) {
			deepest = mount;
		}
	}
	return deepest;
}

[[nodiscard]] std::vector<Mount> numbered(std::span<const Mount> covering, std::uint64_t fsDevice) {
	std::vector<Mount> matching;
	std::ranges::copy_if(covering, std::back_inserter(matching), [fsDevice](const Mount& mount) {
		return mount.fsDevice == fsDevice;
	});
	return matching;
}

} // namespace

std::optional<MountSource> mountSourceFor(
	std::string_view mountInfo,
	const std::filesystem::path& path,
	std::uint64_t fsDevice) {
	const auto covering = coveringMounts(mountInfo, path);
	// The filesystem's own number names the live mount; depth only breaks ties
	// among its bind mounts. Falling back to depth keeps the answer useful when
	// no line carries the number at all.
	const auto onIt = numbered(covering, fsDevice);
	auto deepest = deepestOf(onIt.empty() ? std::span{covering} : std::span{onIt});
	return deepest.has_value() ? std::optional{std::move(deepest->from)} : std::nullopt;
}

} // namespace revenant
