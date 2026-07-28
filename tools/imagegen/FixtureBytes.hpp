// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <vector>

namespace revenant::imagegen {

// Deterministic content of `sizeBytes`, distinct per `seed` so two fixture
// files never read back as each other by accident. Shared by every
// filesystem's image builder — a fixture's *content* carries no filesystem's
// meaning, only the requirement that it be recognizable and reproducible.
[[nodiscard]] std::vector<std::byte> fixtureContent(std::size_t sizeBytes, std::byte seed);

} // namespace revenant::imagegen
