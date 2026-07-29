// SPDX-License-Identifier: GPL-3.0-or-later
#include "revenant/carve/CarverRegistry.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>

#include "revenant/carve/FormatCarver.hpp"
#include "revenant/carve/Signature.hpp"
#include "revenant/carve/SignatureTable.hpp"

namespace revenant::carve {

namespace {

std::size_t widestSignature(const FormatCarver& carver) {
	std::size_t widest = 0;
	for (const Signature& signature : carver.signatures()) {
		widest = std::max(widest, signature.magic.size() + signature.offset);
	}
	return widest;
}

} // namespace

CarverRegistry::CarverRegistry(MatchPath path) noexcept : path_(path) {}

void CarverRegistry::registerCarver(std::unique_ptr<FormatCarver> carver) {
	maxSignatureBytes_ = std::max(maxSignatureBytes_, widestSignature(*carver));
	carvers_.push_back(std::move(carver));
	table_.rebuild(carvers_, path_);
}

std::span<const std::unique_ptr<FormatCarver>> CarverRegistry::carvers() const noexcept {
	return carvers_;
}

std::size_t CarverRegistry::maxSignatureBytes() const noexcept {
	return maxSignatureBytes_;
}

const SignatureTable& CarverRegistry::signatureTable() const noexcept {
	return table_;
}

} // namespace revenant::carve
