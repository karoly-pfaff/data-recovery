// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace revenant::imagegen::exfat {

// One file on the synthetic volume. `contiguous` is the set's own claim that
// its clusters follow one another, which is what lets a deleted exFAT file
// state where its bytes are instead of guessing.
struct ExfatFile {
	std::string_view name;
	std::vector<std::uint32_t> clusters;
	bool live;
	bool contiguous;
	std::vector<std::byte> content;
};

// The root's files: one live and fragmented, one deleted and contiguous, and
// one deleted whose cluster the volume has since handed out again.
[[nodiscard]] std::vector<ExfatFile> rootFiles();

// What lives under `photos`.
[[nodiscard]] std::vector<ExfatFile> photosFiles();

} // namespace revenant::imagegen::exfat
