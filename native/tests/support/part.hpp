#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

// A test case made several ctest entries (`// ctest: parts N` before it, native/CMakeLists.txt): LAB_PART=i/N,
// the i-th of them, does the i-th share of its heaviest loop; alone (no LAB_PART), the whole.
struct Part {
	int index = 0, count = 1;
	// the i-th of the loop's items is this part's: by a hash of i (splitmix64), so that a run of dear items
	// next to each other (a vessel's pixels) is shared among the parts
	bool mine(size_t i) const {
		uint64_t z = static_cast<uint64_t>(i) + 0x9e3779b97f4a7c15ull;
		z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
		z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
		z ^= z >> 31;
		return static_cast<int>(z % static_cast<uint64_t>(count)) == index;
	}
};
inline Part part() {
	static const Part p = [] {
		Part q;
		const char *env = std::getenv("LAB_PART");
		int i = 0, n = 1;
		if (env != nullptr && std::sscanf(env, "%d/%d", &i, &n) == 2 && n >= 1 && i >= 0 && i < n) {
			q.index = i;
			q.count = n;
		}
		return q;
	}();
	return p;
}

// the threads a test's own loop may run on: LAB_THREADS where ctest runs cases side by side, else all
inline unsigned test_threads() {
	const char *env = std::getenv("LAB_THREADS");
	const int asked = env != nullptr ? std::atoi(env) : static_cast<int>(std::thread::hardware_concurrency());
	return asked < 1 ? 1u : static_cast<unsigned>(asked);
}
