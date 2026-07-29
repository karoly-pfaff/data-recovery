// SPDX-License-Identifier: GPL-3.0-or-later
#include "CpuFeatures.hpp"

// The one place in the carve layer that asks the CPU what it can do. Split by
// compiler rather than by platform, because that is what differs: MSVC has
// `__cpuid` and no target attributes, GCC and Clang have `__builtin_cpu_supports`
// and do their own initialization. Both answer the same question.
#ifdef _MSC_VER
#include <intrin.h>

#include <array>
#endif

namespace revenant::carve {

namespace {

#ifdef REVENANT_HAVE_AVX2
constexpr bool kBuiltWithAvx2 = true;
#else
constexpr bool kBuiltWithAvx2 = false;
#endif

#ifdef _MSC_VER

// CPUID leaf 7, sub-leaf 0, EBX bit 5 is AVX2. Reading the leaf at all requires
// the processor to report that it has one, which leaf 0 answers.
constexpr int kExtendedFeatureLeaf = 7;
constexpr unsigned int kAvx2Bit = 1U << 5U;

[[nodiscard]] bool queryAvx2() noexcept {
	std::array<int, 4> registers{};
	__cpuid(registers.data(), 0);
	if (registers.front() < kExtendedFeatureLeaf) {
		return false;
	}
	__cpuidex(registers.data(), kExtendedFeatureLeaf, 0);
	return (static_cast<unsigned int>(registers.at(1)) & kAvx2Bit) != 0;
}

#else

[[nodiscard]] bool queryAvx2() noexcept {
	return __builtin_cpu_supports("avx2");
}

#endif

} // namespace

bool cpuHasAvx2() noexcept {
	return queryAvx2();
}

bool buildHasAvx2() noexcept {
	return kBuiltWithAvx2;
}

} // namespace revenant::carve
