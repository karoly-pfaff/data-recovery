// SPDX-License-Identifier: GPL-3.0-or-later
#include "fs/fat/LongNameAssembly.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <ranges>
#include <vector>

#include "revenant/core/Utf16Name.hpp"
#include "revenant/fs/fat/DirectoryEntry.hpp"

namespace revenant::fs::fat {

namespace {

// A set stored last-first carries ordinals counting *down* to 1, and its first
// slot is flagged as holding the end of the name. A deleted set carries neither,
// its ordinal byte having been overwritten, so it is taken on physical order
// alone.
[[nodiscard]] bool ordinalsDescendToOne(const std::vector<LongNameFragment>& fragments) {
	for (std::size_t at = 0; at < fragments.size(); ++at) {
		if (fragments.at(at).ordinal != fragments.size() - at) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool ordersItself(const std::vector<LongNameFragment>& fragments) {
	return fragments.front().last && ordinalsDescendToOne(fragments);
}

[[nodiscard]] bool anyDeleted(const std::vector<LongNameFragment>& fragments) {
	return std::ranges::any_of(fragments, [](const LongNameFragment& fragment) {
		return fragment.deleted;
	});
}

// Name order is the reverse of the order the slots were passed over in.
[[nodiscard]] std::vector<std::byte> joined(const std::vector<LongNameFragment>& fragments) {
	std::vector<std::byte> utf16le;
	for (const LongNameFragment& fragment : std::views::reverse(fragments)) {
		utf16le.insert(utf16le.end(), fragment.nameBytes.begin(), fragment.nameBytes.end());
	}
	return utf16le;
}

} // namespace

void LongNameBuilder::add(const LongNameFragment& fragment) {
	fragments_.push_back(fragment);
}

void LongNameBuilder::reset() {
	fragments_.clear();
}

std::optional<DecodedName> LongNameBuilder::assembled() const {
	if (fragments_.empty() || fragments_.size() > kMaxNameFragments) {
		return std::nullopt;
	}
	if (!anyDeleted(fragments_) && !ordersItself(fragments_)) {
		return std::nullopt;
	}
	return decodeUtf16Name(joined(fragments_));
}

} // namespace revenant::fs::fat
