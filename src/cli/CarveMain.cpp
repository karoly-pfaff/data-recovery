// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <span>

#include "cli/CarveCli.hpp"

int main(int argc, char** argv) {
	const std::span<char* const> arguments{argv, static_cast<std::size_t>(argc)};
	return revenant::cli::runCarveCli(arguments) ? 0 : 1;
}
