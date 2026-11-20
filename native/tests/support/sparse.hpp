#pragma once

#include <cstdlib>

// How sparsely the heaviest comparisons against the truth are sampled (thousands of exact rays a pixel):
// 1, every sample the test names; LAB_SPARSE=n, every n-th across (the sanitizer build: sanitizers want the
// code's paths, and the release ctest runs every sample).
inline int sparse() {
	static const int n = [] {
		const char *env = std::getenv("LAB_SPARSE");
		const int asked = env != nullptr ? std::atoi(env) : 1;
		return asked < 1 ? 1 : asked;
	}();
	return n;
}
