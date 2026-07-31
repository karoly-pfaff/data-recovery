// SPDX-License-Identifier: GPL-3.0-or-later
// Duplication-gate fixture: a tree with nothing duplicated in it. Structurally
// unlike its neighbour at every point, because the detector unifies identifier
// names — two blocks that differ only in what things are called are clones.

namespace fixture {

int accumulate(const int* values, int count) {
	int total = 0;
	for (int i = 0; i < count; ++i) {
		total += values[i];
	}
	return total;
}

} // namespace fixture
