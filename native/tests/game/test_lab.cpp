#include "lab/lab.hpp"

#include <doctest/doctest.h>

TEST_CASE("lab: the build's name carries GAME_SPEC's version (Stage 0)") {
	CHECK(lab::build_name().rfind("chem-game-v2 ", 0) == 0);
	CHECK(lab::build_name().size() > std::string("chem-game-v2 ").size());
}
