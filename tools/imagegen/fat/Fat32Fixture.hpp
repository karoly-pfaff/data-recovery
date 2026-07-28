// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace revenant::imagegen::fat {

// One file on the synthetic volume. `longName` is empty when the file has only
// its 8.3 name; `clusters` spells out where its content went, so a fragmented
// file is a visible layout decision rather than something a builder derived.
struct Fat32File {
	std::string_view shortName; // 11 bytes, space-padded
	std::string_view longName;
	std::vector<std::uint32_t> clusters;
	bool deleted;
	std::vector<std::byte> content;
};

// Deterministic content of `sizeBytes`, distinct per `seed` so two fixture
// files never read back as each other by accident.
[[nodiscard]] std::vector<std::byte> fixtureContent(std::size_t sizeBytes, std::byte seed);

// The four files the fixture volume holds: one live in the root, one deleted in
// the root, one live and fragmented under `photos` with a long name, one
// deleted under `photos`, and one deleted inside a deleted directory — which is
// what makes it an orphan.
[[nodiscard]] std::vector<Fat32File> fat32FixtureFiles();

} // namespace revenant::imagegen::fat
