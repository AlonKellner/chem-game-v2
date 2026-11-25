// The calculator (GAME_SPEC §13, Stage 58): molar masses from the tables' atomic weights, moles, a solution's mass,
// a dilution, a reaction's stoichiometry, its limiting reagent and yields.
#include "lab/calculator.hpp"

#include "chem/builtin_ids.hpp"

#include <doctest/doctest.h>

#include <cmath>

using namespace lab::calc;

namespace {

const chem::Reaction &reaction(int index) {
	return chem::builtin_reactions()[static_cast<size_t>(index)];
}

} // namespace

TEST_CASE("calculator: a formula's molar mass and its composition, from the tables' atomic weights (Stage 58)") {
	const chem::SpeciesTable &t = chem::builtin_species();
	const MolarMass penta = molar_mass(t, "CuSO4.5H2O");
	REQUIRE_MESSAGE(penta.ok, penta.why);
	CHECK(penta.g_per_mol == doctest::Approx(63.546 + 32.06 + 9 * 15.999 + 10 * 1.008).epsilon(1e-12)); // 249.68
	CHECK(penta.g_per_mol == doctest::Approx(static_cast<double>(t.molar_mass_ug(chem::builtin::copper_sulfate_pentahydrate)) * 1e-6).epsilon(1e-12));
	double share = 0.0;
	for (const auto &[symbol, pct] : penta.shares) {
		share += pct;
		if (symbol == "Cu") {
			CHECK(pct == doctest::Approx(100.0 * 63.546 / 249.68).epsilon(1e-4)); // 25.45 %
		}
	}
	CHECK(share == doctest::Approx(100.0));
	CHECK(molar_mass(t, "Cu2CO3(OH)2").g_per_mol == doctest::Approx(221.11).epsilon(1e-4)); // malachite
	const MolarMass gold = molar_mass(t, "AuCl3");
	CHECK_FALSE(gold.ok);
	CHECK(gold.why.find("Au") != std::string::npos);
	CHECK_FALSE(molar_mass(t, "cuso4").ok);
	CHECK_FALSE(molar_mass(t, "").ok);
}

TEST_CASE("calculator: moles, a solution's mass and a dilution (Stage 58)") {
	CHECK(moles(2.497, 249.68) == doctest::Approx(0.0100008).epsilon(1e-5));
	CHECK(grams(0.01, 249.68) == doctest::Approx(2.4968));
	CHECK(solution_grams(0.0400, 250.0, 249.68) == doctest::Approx(2.4968)); // 250 mL of 0.0400 M from the pentahydrate
	CHECK(dilution_ml(0.100, 0.0100, 100.0) == doctest::Approx(10.0));
}

TEST_CASE("calculator: a reaction's stoichiometry, its limiting reagent and its yield (Stage 58)") {
	const chem::SpeciesTable &t = chem::builtin_species();
	const chem::Reaction &r = reaction(chem::builtin::reaction::copper_oxide_dissolution); // CuO + H2SO4 -> CuSO4 + H2O
	CHECK(equation(t, r) == "CuO(s) + H2SO4(aq) -> CuSO4(aq) + H2O(l)");
	// 2.00 g of the oxide and 0.0100 mol of the acid (0.98 g): the acid limits
	const Stoichiometry s = stoichiometry(t, r, { 2.00, 0.0100 * 98.072, std::nullopt, std::nullopt });
	REQUIRE_MESSAGE(s.ok, s.why);
	CHECK(s.limiting == 1);
	CHECK(s.extent == doctest::Approx(0.0100).epsilon(1e-6));
	CHECK(s.terms[2].g == doctest::Approx(0.0100 * 159.60).epsilon(1e-4)); // the copper sulfate it can make
	CHECK(s.terms[0].g == doctest::Approx(0.0100 * 79.545).epsilon(1e-4)); // the oxide it uses
	CHECK(percent_yield(1.436, s.terms[2].g) == doctest::Approx(90.0).epsilon(0.001));
	// a product's mass alone says how far it went
	const Stoichiometry back = stoichiometry(t, r, { std::nullopt, std::nullopt, 1.596, std::nullopt });
	REQUIRE(back.ok);
	CHECK(back.terms[0].g == doctest::Approx(0.79545).epsilon(1e-3));
	CHECK_FALSE(stoichiometry(t, r, {}).ok);
	CHECK_FALSE(stoichiometry(t, r, { -1.0 }).ok);
	// malachite's decomposition, two of the oxide a formula
	const chem::Reaction &m = reaction(chem::builtin::reaction::malachite_decomposition);
	const Stoichiometry heat = stoichiometry(t, m, { 2.211 });
	REQUIRE(heat.ok);
	CHECK(heat.terms[1].g == doctest::Approx(2.0 * 0.01 * 79.545).epsilon(1e-3)); // 1.591 g of the oxide from 2.211 g
}
