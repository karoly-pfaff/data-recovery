// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/UndeleteCli.hpp"

#include <span>
#include <string_view>

#include "cli/Frontend.hpp"
#include "cli/UndeleteOptions.hpp"

namespace revenant::cli {

namespace {

constexpr std::string_view kUsage =
	"usage: revenant-undelete --source <image> --destination <directory>\n"
	"                        [--hybrid | --fs-only | --carve-only]\n"
	"                        [--session <directory>]";

} // namespace

bool runUndeleteCli(std::span<char* const> args) {
	return runFrontend(args, kUsage, parseUndeleteOptions);
}

} // namespace revenant::cli
