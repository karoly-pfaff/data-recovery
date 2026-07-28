// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace revenant::imagegen::exfat {

inline constexpr std::size_t kSlotBytes = 32;

// One entry set as the fixture wants it. `live` sets the in-use bit on each of
// the set's three type bytes — the single bit exFAT clears to delete a file.
struct SetSpec {
	std::string_view name;
	std::uint32_t firstCluster;
	std::uint64_t sizeInBytes;
	bool live;
	bool contiguous;
	bool isDirectory;
};

// The three slots of a file's entry set: the file entry, the stream extension,
// and one name fragment. A name longer than 15 code units would need more
// fragments; the fixture keeps its names shorter than that on purpose, so what
// the walk is being asked to prove stays visible.
[[nodiscard]] std::vector<std::byte> entrySet(const SetSpec& spec);

// Where a volume says its allocation bitmap is.
struct BitmapSpec {
	std::uint32_t firstCluster;
	std::uint64_t lengthBytes;
};

[[nodiscard]] std::vector<std::byte> bitmapEntry(const BitmapSpec& spec);

} // namespace revenant::imagegen::exfat
