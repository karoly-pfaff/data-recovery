// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/Interrupt.hpp"

#include <csignal>

namespace revenant::cli {

namespace {

// The only thing a signal handler may portably touch. Raised on the way in and
// read by the run between chunks — the handler itself decides nothing. The one
// piece of state a signal handler is allowed to have, so it cannot be const.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile std::sig_atomic_t gInterrupted = 0;

extern "C" void onInterrupt(int /*signal*/) {
	gInterrupted = 1;
}

} // namespace

void catchInterrupts() {
	gInterrupted = 0;
	// The result is deliberately not checked: a platform that will not install
	// the handler leaves the run uninterruptible, which is the behaviour there
	// was before it, not a failure worth ending a recovery over.
	static_cast<void>(std::signal(SIGINT, onInterrupt));
}

bool interrupted() noexcept {
	return gInterrupted != 0;
}

} // namespace revenant::cli
