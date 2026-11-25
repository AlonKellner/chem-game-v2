// The planner (GAME_SPEC §13, Stage 60): a target worked back along the route the student chooses.
#include "lab/planner.hpp"

#include "chem/builtin_ids.hpp"

#include <doctest/doctest.h>

using namespace lab::plan;

TEST_CASE("planner: copper sulfate crystals worked back to malachite, by the oxide or directly, with the yields expected (Stage 60)") {
	const chem::SpeciesTable &t = chem::builtin_species();
	namespace r = chem::builtin::reaction;
	// 5.00 g of the pentahydrate (0.020025 mol): crystallized from the solution, made from the oxide in acid, the oxide
	// from malachite heated
	const Plan by_oxide = work_back(t, chem::builtin::copper_sulfate_pentahydrate, 5.00,
			{ { r::pentahydrate_crystallization, 1.0 }, { r::copper_oxide_dissolution, 1.0 }, { r::malachite_decomposition, 1.0 } });
	REQUIRE_MESSAGE(by_oxide.ok, by_oxide.why);
	const double n = 5.00 / 249.68;
	double malachite = 0.0, acid = 0.0, oxide = 0.0;
	for (const Need &need : by_oxide.needs) {
		malachite += need.species.index == chem::builtin::malachite.index ? need.g : 0.0;
		acid += need.species.index == chem::builtin::sulfuric_acid_aq.index ? need.mol : 0.0;
		oxide += need.species.index == chem::builtin::copper_oxide.index && need.carried ? need.g : 0.0;
	}
	CHECK(oxide == doctest::Approx(n * 79.545).epsilon(1e-4)); // 1.593 g of the oxide, carried
	CHECK(acid == doctest::Approx(n).epsilon(1e-6)); // a mole of acid to each
	CHECK(malachite == doctest::Approx(n / 2.0 * 221.11).epsilon(1e-4)); // two of the oxide from each malachite: 2.214 g
	// expecting 60 % of it to crystallize: everything before it, more by 1 / 0.6
	const Plan expecting = work_back(t, chem::builtin::copper_sulfate_pentahydrate, 5.00,
			{ { r::pentahydrate_crystallization, 0.6 }, { r::copper_oxide_dissolution, 1.0 }, { r::malachite_decomposition, 1.0 } });
	REQUIRE(expecting.ok);
	CHECK(expecting.extents[2] == doctest::Approx(by_oxide.extents[2] / 0.6));
	// directly: malachite in the acid, two of the sulfate from each
	const Plan direct = work_back(t, chem::builtin::copper_sulfate_pentahydrate, 5.00, { { r::pentahydrate_crystallization, 1.0 }, { r::malachite_dissolution, 1.0 } });
	REQUIRE(direct.ok);
	CHECK(direct.extents[1] == doctest::Approx(n / 2.0));
	// refused: a step that makes nothing the one before needs; a reaction that does not make the target
	CHECK_FALSE(work_back(t, chem::builtin::copper_sulfate_pentahydrate, 5.00, { { r::pentahydrate_crystallization, 1.0 }, { r::hydroxide_precipitation, 1.0 } }).ok);
	CHECK_FALSE(work_back(t, chem::builtin::copper_sulfate_pentahydrate, 5.00, { { r::copper_oxide_dissolution, 1.0 } }).ok);
	CHECK_FALSE(work_back(t, chem::builtin::copper_sulfate_pentahydrate, 0.0, { { r::pentahydrate_crystallization, 1.0 } }).ok);
	CHECK(makers(chem::builtin::copper_oxide).size() >= 2); // malachite heated, the hydroxide dehydrated
}
