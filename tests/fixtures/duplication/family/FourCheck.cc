// SPDX-License-Identifier: GPL-3.0-or-later
// Duplication-gate fixture: see OneCheck.cc.

namespace fixture::four {

bool inRange(unsigned int value, unsigned int low, unsigned int high) {
	if (value < low) {
		return false;
	}
	if (value > high) {
		return false;
	}
	return true;
}

double average(const double* samples, int count) {
	double total = 0.0;
	int taken = 0;
	while (taken < count) {
		total += samples[taken];
		taken = taken + 1;
	}
	return count > 0 ? total / count : 0.0;
}

} // namespace fixture::four
