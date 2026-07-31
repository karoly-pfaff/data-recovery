// SPDX-License-Identifier: GPL-3.0-or-later
// Duplication-gate fixture: a short block with four homes. Its copies are well
// under any threshold worth setting, but four of them together are not — which
// is what the detector's own threshold measures, and what this gate does not.
//
// The shared block comes first in each of the four files on purpose: the
// detector numbers unified identifiers per scope, so a distinct function ahead
// of it would shift the numbering and the copies would stop matching.

namespace fixture::one {

bool inRange(unsigned int value, unsigned int low, unsigned int high) {
	if (value < low) {
		return false;
	}
	if (value > high) {
		return false;
	}
	return true;
}

int firstSetBit(unsigned int mask) {
	for (int bit = 0; bit < 32; ++bit) {
		if ((mask >> bit) & 1U) {
			return bit;
		}
	}
	return -1;
}

} // namespace fixture::one
