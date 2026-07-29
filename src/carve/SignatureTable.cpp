// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/carve/SignatureTable.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "revenant/carve/FormatCarver.hpp"
#include "revenant/carve/Signature.hpp"

namespace revenant::carve {

namespace {

[[nodiscard]] std::size_t indexOf(std::byte value) noexcept {
	return std::to_integer<std::size_t>(value);
}

// A signature with no magic would match at every position, so it is not a
// signature; leaving it out is the only reading that keeps a scan finite.
void appendSignatures(
	std::vector<SignatureEntry>& entries,
	const FormatCarver& carver,
	std::uint32_t carverIndex) {
	for (const Signature& signature : carver.signatures()) {
		if (!signature.magic.empty()) {
			entries.push_back(
				SignatureEntry{
					.magic = signature.magic,
					.inFileOffset = signature.offset,
					.carver = &carver,
					.carverIndex = carverIndex});
		}
	}
}

// Every registered signature, in registration order.
[[nodiscard]] std::vector<SignatureEntry>
flatten(std::span<const std::unique_ptr<FormatCarver>> carvers) {
	std::vector<SignatureEntry> entries;
	std::uint32_t carverIndex = 0;
	for (const std::unique_ptr<FormatCarver>& carver : carvers) {
		appendSignatures(entries, *carver, carverIndex);
		++carverIndex;
	}
	return entries;
}

} // namespace

void SignatureTable::rebuild(std::span<const std::unique_ptr<FormatCarver>> carvers) {
	auto flat = flatten(carvers);
	// Stable, so registration order survives inside a group — which is what
	// makes the match order below a contract rather than a sorting accident.
	std::ranges::stable_sort(flat, {}, [](const SignatureEntry& entry) {
		return entry.magic.front();
	});
	entries_ = std::move(flat);
	indexGroups();
}

// One past the last entry whose magic starts with `value`. Its group's start
// has already been recorded, and the entries are sorted by that byte, so the
// group is a contiguous run from there.
std::size_t SignatureTable::groupEnd(std::size_t value) const noexcept {
	std::size_t at = groupBegin_.at(value);
	while (at < entries_.size() && indexOf(entries_.at(at).magic.front()) == value) {
		++at;
	}
	return at;
}

void SignatureTable::indexGroups() noexcept {
	std::size_t at = 0;
	for (std::size_t value = 0; value < kByteValues; ++value) {
		groupBegin_.at(value) = static_cast<std::uint16_t>(at);
		const auto end = groupEnd(value);
		anyStartsWith_.at(value) = static_cast<std::uint8_t>(end > at ? 1 : 0);
		at = end;
	}
	groupBegin_.at(kByteValues) = static_cast<std::uint16_t>(at);
}

std::span<const SignatureEntry> SignatureTable::startingWith(std::byte first) const noexcept {
	// A group's end is the next group's start, so both are one two-element
	// slice.
	const auto bounds = std::span{groupBegin_}.subspan(indexOf(first), 2);
	return std::span<const SignatureEntry>{entries_}.subspan(
		bounds.front(),
		static_cast<std::size_t>(bounds.back()) - bounds.front());
}

std::size_t SignatureTable::size() const noexcept {
	return entries_.size();
}

} // namespace revenant::carve
