// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace revenant::recovery {

// ADR-0010 step 4 collision suffixing: the number of numbered candidates
// ("name (2).ext" .. "name (10001).ext") `disambiguate` will try before
// giving up on the scheme and falling back to an unconditional suffix — see
// Disambiguate.cpp. Named so the bound is never a re-typed magic number.
inline constexpr int kMaxDisambiguationAttempts = 10000;

// Deterministic collision suffixing (ADR-0010 step 4): returns `desired`
// unchanged if `taken(desired)` is false. Otherwise tries "name (2).ext",
// "name (3).ext", ... — the suffix inserted before the LAST extension dot
// (appended outright for an extensionless name) — returning the first
// candidate for which `taken` answers false. `taken` answers "is this name
// already used"; first free wins. Bounded at kMaxDisambiguationAttempts
// numbered candidates: past the bound (every candidate reports taken),
// returns `desired + " (overflow)" + <counter>` unconditionally, so the
// function always terminates regardless of `taken`'s behavior.
[[nodiscard]] std::string
disambiguate(std::string_view desired, const std::function<bool(std::string_view)>& taken);

} // namespace revenant::recovery
