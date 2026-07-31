// SPDX-License-Identifier: GPL-3.0-or-later
// Duplication-gate fixture: see OneCheck.cc.

namespace fixture::two {

bool inRange(unsigned int value, unsigned int low, unsigned int high) {
	if (value < low) {
		return false;
	}
	if (value > high) {
		return false;
	}
	return true;
}

char grade(int score) {
	switch (score / 10) {
	case 10:
	case 9:
		return 'a';
	case 8:
		return 'b';
	default:
		return 'f';
	}
}

} // namespace fixture::two
