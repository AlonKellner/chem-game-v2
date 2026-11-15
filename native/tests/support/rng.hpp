#pragma once

#include <cstdint>

// splitmix64: the tests' random numbers, the same on every platform.
struct Rng {
	uint64_t state;
	explicit Rng(uint64_t seed) :
			state(seed) {}
	uint64_t next() {
		uint64_t z = (state += 0x9e3779b97f4a7c15ull);
		z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
		z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
		return z ^ (z >> 31);
	}
	// uniform in [lo, hi]
	int64_t range(int64_t lo, int64_t hi) { return lo + static_cast<int64_t>(next() % static_cast<uint64_t>(hi - lo + 1)); }
	bool chance(int percent) { return range(0, 99) < percent; }
};
