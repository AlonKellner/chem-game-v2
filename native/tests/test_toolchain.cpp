// Stage 0: the toolchain keeps the floating-point rules the replays rely on.
#include "chem/fp_check.hpp"
#include "chem/version.hpp"

#include <doctest/doctest.h>

#include <cstring>

TEST_CASE("toolchain: a*b+c is two roundings, never a fused one (Stage 0)") {
	// a*b = 1 - 2^-60 exactly, which rounds to 1.0; fused, a*b+c = -2^-60.
	const double a = 1.0 + 0x1p-30;
	const double b = 1.0 - 0x1p-30;
	const double c = -1.0;
	CHECK(chem::fma_sensitive(a, b, c) == 0.0);
}

TEST_CASE("toolchain: the build names GAME_SPEC's version (Stage 0)") {
	CHECK(std::strlen(chem::SPEC_VERSION) > 0);
}
