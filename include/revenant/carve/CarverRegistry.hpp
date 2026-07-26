// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "revenant/carve/FormatCarver.hpp"

namespace revenant::carve {

// Owns every registered FormatCarver and answers the combined-signature
// questions the scanner needs.
class CarverRegistry {
public:
	void registerCarver(std::unique_ptr<FormatCarver> carver);

	[[nodiscard]] std::span<const std::unique_ptr<FormatCarver>> carvers() const noexcept;

	// Longest (magic size + in-file offset) over all signatures — the
	// cross-window overlap the scanner must keep to never miss a match.
	[[nodiscard]] std::size_t maxSignatureBytes() const noexcept;

private:
	std::vector<std::unique_ptr<FormatCarver>> carvers_;
	std::size_t maxSignatureBytes_ = 0;
};

} // namespace revenant::carve
