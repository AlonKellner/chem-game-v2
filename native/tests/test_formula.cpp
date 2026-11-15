// The data files' formulas and equations (native/datagen/formula.hpp).
#include "formula.hpp"

#include <doctest/doctest.h>

using chemdata::AtomCounts;

TEST_CASE("formula: groups, counts and hydrate parts add up (Stage 1)") {
	AtomCounts atoms;
	std::string why;
	REQUIRE(chemdata::parse_formula("Cu2CO3(OH)2", atoms, why));
	CHECK(atoms == AtomCounts{ { "Cu", 2 }, { "C", 1 }, { "O", 5 }, { "H", 2 } });
	REQUIRE(chemdata::parse_formula("CuSO4.5H2O", atoms, why));
	CHECK(atoms == AtomCounts{ { "Cu", 1 }, { "S", 1 }, { "O", 9 }, { "H", 10 } });
	REQUIRE(chemdata::parse_formula("Fe2(SO4)3", atoms, why));
	CHECK(atoms == AtomCounts{ { "Fe", 2 }, { "S", 3 }, { "O", 12 } });
	REQUIRE(chemdata::parse_formula("Ca(H2(PO4))2", atoms, why));
	CHECK(atoms == AtomCounts{ { "Ca", 1 }, { "H", 4 }, { "P", 2 }, { "O", 8 } });
}

TEST_CASE("formula: a malformed formula is refused with a reason (Stage 1)") {
	AtomCounts atoms;
	for (const char *bad : { "", "h2o", "H2O)", "(H2O", "H0", "H2O.", ".H2O", "H2 O", "2", "H1234567" }) {
		std::string why;
		CAPTURE(bad);
		CHECK_FALSE(chemdata::parse_formula(bad, atoms, why));
		CHECK_FALSE(why.empty());
	}
}

TEST_CASE("formula: an equation's terms carry their side as the sign (Stage 1)") {
	std::vector<chemdata::EquationTerm> terms;
	std::string why;
	REQUIRE(chemdata::parse_equation("Cu2CO3(OH)2(s) -> 2 CuO(s) + CO2(g) + H2O(g)", terms, why));
	REQUIRE(terms.size() == 4);
	CHECK(terms[0].species == "Cu2CO3(OH)2(s)");
	CHECK(terms[0].nu == -1);
	CHECK(terms[1].species == "CuO(s)");
	CHECK(terms[1].nu == 2);
	CHECK(terms[3].species == "H2O(g)");
	CHECK(terms[3].nu == 1);
	CHECK_FALSE(chemdata::parse_equation("H2O(l) = H2O(g)", terms, why));
	CHECK_FALSE(chemdata::parse_equation("H2O(l) -> H2O(g) -> H2O(s)", terms, why));
	CHECK_FALSE(chemdata::parse_equation("H2O(l) ->", terms, why));
}
