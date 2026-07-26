// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "revenant/carve/CarveResult.hpp"
#include "revenant/carve/Signature.hpp"
#include "revenant/core/ByteReader.hpp"
#include "revenant/core/Result.hpp"

namespace revenant::carve {

// One file format's validating parser: recognizes its signatures and walks
// the format's structure to the exact extent. A carver only ever returns a
// verdict — extraction decisions belong to arbitration (ADR-0006).
class FormatCarver {
public:
	virtual ~FormatCarver() = default;
	FormatCarver() = default;
	FormatCarver(const FormatCarver&) = delete;
	FormatCarver& operator=(const FormatCarver&) = delete;
	FormatCarver(FormatCarver&&) = delete;
	FormatCarver& operator=(FormatCarver&&) = delete;

	// The signatures that trigger a validation attempt for this format.
	[[nodiscard]] virtual std::span<const Signature> signatures() const = 0;

	// Walk the structure from the reader's start (= the candidate's first
	// byte) and return the exact extent + verdict, or a typed error.
	[[nodiscard]] virtual Result<CarveResult> carve(ByteReader& reader) const = 0;
};

} // namespace revenant::carve
