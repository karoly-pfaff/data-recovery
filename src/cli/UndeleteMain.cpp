// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <span>

#include "cli/UndeleteCli.hpp"

int main(int argc, char** argv) {
	const std::span<char* const> args{argv, static_cast<std::size_t>(argc)};
	// The exit status is the outcome's own number: what the caller should do
	// next, documented in README.md and both --help texts (story-0605).
	return static_cast<int>(revenant::cli::runUndeleteCli(args));
}
