// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace revenant::recovery {

// Whether the operator has stated they checked what the tool could not.
//
// ADR-0005's destination rule refuses when it cannot resolve either side's
// physical identity, and a VeraCrypt volume has none — Windows maps no
// partition or disk behind it, which is the question the rule asks. With such a
// volume as the *source* that refused every destination, so an encrypted disk
// could not be recovered from at all: not degraded, not warned, refused.
//
// This is the operator answering that question. It may relax the unresolvable
// case and must never touch the proven one — if the tool can show the
// destination sits on the source, an assertion that someone checked is simply
// wrong. That separation is the whole safety value, so it is a named type
// rather than a bare `bool` travelling through three signatures.
//
// Public because `RecoverySink::open` takes it; the rule that consumes it
// (`recovery/DestinationRule.hpp`) is internal.
enum class UnverifiedIdentity : std::uint8_t {
	// The default, and what every run does unless told otherwise.
	kRefuse,
	// The operator has confirmed the destination is not on the source. The run
	// records that it started on an unverified identity.
	kAllow,
};

} // namespace revenant::recovery
