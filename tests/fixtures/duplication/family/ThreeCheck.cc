// SPDX-License-Identifier: GPL-3.0-or-later
// Duplication-gate fixture: see OneCheck.cc.

namespace fixture::three {

bool inRange(unsigned int value, unsigned int low, unsigned int high) {
	if (value < low) {
		return false;
	}
	if (value > high) {
		return false;
	}
	return true;
}

unsigned int gcd(unsigned int left, unsigned int right) {
	while (right != 0) {
		const unsigned int remainder = left % right;
		left = right;
		right = remainder;
	}
	return left;
}

} // namespace fixture::three
