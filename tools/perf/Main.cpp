// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <span>

#include "perf/BenchMain.hpp"

int main(int argc, char** argv) {
	return revenant::perf::runBenchmarks(
		std::span<char* const>{argv, static_cast<std::size_t>(argc)});
}
