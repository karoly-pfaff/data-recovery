// SPDX-License-Identifier: GPL-3.0-or-later
// Duplication-gate fixture: see Accumulate.cc.

namespace fixture {

char classify(char letter) {
	switch (letter) {
	case 'a':
	case 'e':
		return 'v';
	case '0':
		return 'd';
	default:
		return '?';
	}
}

} // namespace fixture
