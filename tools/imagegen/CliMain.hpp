// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string>

namespace revenant::imagegen {

// Every verb this tool dispatches, with the operands it takes — built from the
// verb table itself, so a verb cannot exist without a line documenting it.
// Exposed because that guarantee is worth asserting: `runCli` logs through a
// sink it builds itself, which a test cannot reach.
[[nodiscard]] std::string usageText();

// Parses one of the forms `usageText()` lists and generates the image. Returns
// false (after logging to stderr) on any usage or write error.
[[nodiscard]] bool runCli(std::span<char* const> args);

} // namespace revenant::imagegen
