// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery/ManifestJson.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace revenant::recovery::json {

namespace {

constexpr char kLowestPrintable = 0x20;
constexpr std::string_view kHexDigits = "0123456789abcdef";
constexpr unsigned int kHighNibbleShift = 4;
constexpr unsigned int kLowNibbleMask = 0x0F;

// A control character as its six-character escape. Anything below the printable
// range has to go through this, or a filename holding a newline would end the
// string it is inside.
[[nodiscard]] std::string escapedControl(unsigned char value) {
	std::string escape{"\\u00"};
	escape.push_back(kHexDigits.at(value >> kHighNibbleShift));
	escape.push_back(kHexDigits.at(value & kLowNibbleMask));
	return escape;
}

[[nodiscard]] std::string escaped(char value) {
	if (value == '"' || value == '\\') {
		return std::string{'\\'} + value;
	}
	if (value >= kLowestPrintable || value < 0) {
		return std::string{value};
	}
	return escapedControl(static_cast<unsigned char>(value));
}

// A path's own bytes as UTF-8, whatever narrow encoding this platform prefers.
[[nodiscard]] std::string utf8Of(const std::filesystem::path& path) {
	const auto encoded = path.u8string();
	std::string text;
	text.reserve(encoded.size());
	for (const char8_t unit : encoded) {
		text.push_back(static_cast<char>(unit));
	}
	return text;
}

// The items of a JSON list, separated but not yet bracketed.
[[nodiscard]] std::string joined(std::span<const std::string> items) {
	std::string text;
	for (const std::string& item : items) {
		text += text.empty() ? "" : ",";
		text += item;
	}
	return text;
}

} // namespace

std::string quotedText(std::string_view text) {
	std::string json{'"'};
	for (const char value : text) {
		json += escaped(value);
	}
	json.push_back('"');
	return json;
}

std::string quotedPath(const std::filesystem::path& path) {
	return quotedText(utf8Of(path));
}

std::string member(std::string_view name, std::string_view text) {
	return quotedText(name) + ":" + quotedText(text);
}

std::string member(std::string_view name, std::uint64_t value) {
	return quotedText(name) + ":" + std::to_string(value);
}

std::string member(std::string_view name, bool value) {
	return quotedText(name) + ":" + (value ? "true" : "false");
}

std::string rawMember(std::string_view name, std::string_view value) {
	return quotedText(name) + ":" + std::string{value};
}

std::string object(std::span<const std::string> members) {
	return "{" + joined(members) + "}";
}

std::string array(std::span<const std::string> items) {
	return "[" + joined(items) + "]";
}

} // namespace revenant::recovery::json
