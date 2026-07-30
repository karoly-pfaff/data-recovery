// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to src/carve/ — asked once, when a signature table is built.

namespace revenant::carve {

// Whether this CPU can execute the AVX2 reject step. Asked at most once per
// scan: a check inside the loop would be measurable in the loop it exists to
// speed up, and the machines people run recovery tools on are old machines, so
// the answer is genuinely sometimes no.
[[nodiscard]] bool cpuHasAvx2() noexcept;

// Whether this build contains an AVX2 reject step at all. False where the
// compiler or the target could not produce one, which makes `cpuHasAvx2`
// irrelevant rather than wrong.
[[nodiscard]] bool buildHasAvx2() noexcept;

} // namespace revenant::carve
