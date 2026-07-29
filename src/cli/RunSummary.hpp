// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "cli/RecoveryRun.hpp"
#include "revenant/core/Error.hpp"

namespace revenant::cli {

// A finished run in the three sentences an operator needs: what was found,
// what arbitration chose from it, and what reached the destination.
//
// `RecoveryStats::regionsDropped` is deliberately absent. A dropped accounting
// region means the carve pass searches *more* of the device (ADR-0009), never
// that a file is lost, so it is a performance fact rather than a recovery one;
// the complete per-run record is the session manifest (story-0115).
[[nodiscard]] std::vector<std::string> summarize(const RunReport& report);

// Why a run stopped, in words rather than an enumerator.
[[nodiscard]] std::string describe(const Error& error);

} // namespace revenant::cli
