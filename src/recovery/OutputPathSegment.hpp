// SPDX-License-Identifier: GPL-3.0-or-later
// Internal to the sanitizeOutputPath pipeline (ADR-0009) — not a public
// interface, subject to change without notice. Owns turning one untrusted
// raw name into a bounded list of safe, cleaned path segments; path
// assembly and containment verification live in OutputPath.cpp.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "revenant/core/Result.hpp"

namespace revenant::recovery {

// Splits `name` on '/' and '\', drops empty and "." segments, rejects any
// ".." or drive/volume-prefix ("C:...") segment outright, neutralizes
// Windows-reserved basenames (CON, PRN, AUX, NUL, COM1-9, LPT1-9 — matched
// case-insensitively against the basename before the first dot), strips
// trailing dots/spaces (a segment left empty by stripping becomes "_"), and
// enforces the per-segment and total-segment-count bounds
// (kMaxSegmentBytes/kMaxSegments). Every rejection reason reports
// `kInvalidArgument` — see OutputPath.hpp.
[[nodiscard]] Result<std::vector<std::string>> collectSegments(std::string_view name);

} // namespace revenant::recovery
