// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/fat/ShortName.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include "fs/NameEscape.hpp"
#include "revenant/fs/NameDecode.hpp"

namespace revenant::fs::fat {

namespace {

constexpr std::size_t kBaseBytes = 8;
constexpr std::size_t kExtensionBytes = 3;
constexpr std::byte kPadding{0x20};
constexpr std::byte kDeletionMarker{0xE5};
// A live name whose real first character is 0xE5 stores 0x05 instead, because
// 0xE5 in that byte means "deleted". Japanese-locale volumes rely on it.
constexpr std::byte kEscapedFirstCharacter{0x05};

constexpr std::uint8_t kLowerCaseBase = 0x08;
constexpr std::uint8_t kLowerCaseExtension = 0x10;

// The accumulating name and whether every byte so far survived as itself.
struct NameState {
	std::string utf8;
	bool lossless = true;
};

// `/` would split a volume-relative path and `%` would make an escape
// ambiguous; neither may pass through, whatever the code page says.
[[nodiscard]] bool passesThrough(std::byte raw) noexcept {
	const auto value = std::to_integer<unsigned>(raw);
	return value >= 0x20U && value <= 0x7EU && raw != std::byte{'/'} && raw != std::byte{'%'};
}

void appendByte(NameState& state, std::byte raw, bool toLower) {
	if (!passesThrough(raw)) {
		appendEscapedByte(state.utf8, raw);
		state.lossless = false;
		return;
	}
	const auto value = std::to_integer<unsigned char>(raw);
	state.utf8.push_back(static_cast<char>(toLower ? std::tolower(value) : value));
}

// The field without its trailing space padding. FAT pads both halves of an 8.3
// name to a fixed width; the padding is layout, not name.
[[nodiscard]] std::span<const std::byte> trimmed(std::span<const std::byte> field) {
	while (!field.empty() && field.back() == kPadding) {
		field = field.first(field.size() - 1);
	}
	return field;
}

void appendField(NameState& state, std::span<const std::byte> field, bool toLower) {
	for (const std::byte raw : trimmed(field)) {
		appendByte(state, raw, toLower);
	}
}

// The base name's first byte, restored to whatever it can be: the character
// 0x05 stands in for, the placeholder a deletion destroyed it with, or itself.
void appendFirstByte(NameState& state, std::byte raw, bool deleted, bool toLower) {
	if (deleted) {
		state.utf8.push_back(kLostFirstCharacter);
		state.lossless = false;
		return;
	}
	appendByte(state, raw == kEscapedFirstCharacter ? kDeletionMarker : raw, toLower);
}

void appendBase(
	NameState& state,
	std::span<const std::byte> base,
	std::uint8_t flags,
	bool deleted) {
	const bool toLower = (flags & kLowerCaseBase) != 0;
	const auto body = trimmed(base);
	if (body.empty()) {
		return;
	}
	appendFirstByte(state, body.front(), deleted, toLower);
	appendField(state, body.subspan(1), toLower);
}

void appendExtension(NameState& state, std::span<const std::byte> extension, std::uint8_t flags) {
	if (trimmed(extension).empty()) {
		return;
	}
	state.utf8.push_back('.');
	appendField(state, extension, (flags & kLowerCaseExtension) != 0);
}

} // namespace

DecodedName decodeShortName(std::span<const std::byte> raw, std::uint8_t caseFlags, bool deleted) {
	if (raw.size() < kBaseBytes + kExtensionBytes) {
		return DecodedName{.utf8 = {}, .lossless = false};
	}
	NameState state;
	appendBase(state, raw.first(kBaseBytes), caseFlags, deleted);
	appendExtension(state, raw.subspan(kBaseBytes, kExtensionBytes), caseFlags);
	return DecodedName{.utf8 = std::move(state.utf8), .lossless = state.lossless};
}

} // namespace revenant::fs::fat
