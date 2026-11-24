// Acids, bases and pH (GAME_SPEC §9, Stage 43): water's ionisation against IAPWS, the base's and its salt's
// activity against their evaluated tables, the neutralisation's heat from the tables, and the pH.
#include "chem/builtin_ids.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace chem;

namespace {

int64_t quanta(double n) {
	bool ok = false;
	const Amount a = amount_from_mol(n, ok);
	REQUIRE(ok);
	return a.quanta;
}

// a kilogram of water and what is put in it
struct Cell {
	CellState state{ std::vector<int64_t>(static_cast<size_t>(builtin_species().species_count()), 0), 0,
		std::vector<int64_t>(static_cast<size_t>(builtin_species().species_count()), 0) };
	Cell() { put(builtin::water, 1.0 / 0.018015); }
	Cell &put(SpeciesId s, double mol) {
		state.amount[static_cast<size_t>(s.index)] = quanta(mol);
		return *this;
	}
};

// within the 2.5 % the Pitzer parameters keep to the tables (Pitzer and Mayorga's own fit is to about 2 %)
bool near_enough(double model, double measured) {
	return std::abs(model / measured - 1.0) < 0.025;
}

double ph_of(const Cell &c, double kelvin = 298.15) {
	double p = 0.0;
	REQUIRE(ph(builtin_species(), c.state, kelvin, p));
	return p;
}

} // namespace

TEST_CASE("measured: water's ionisation constant is IAPWS R11-24's from 0 to 100 C (Stage 43)") {
	// pKw at saturation, IAPWS R11-24 (2024; Arcis et al., J. Phys. Chem. Ref. Data 53, 023103), Table 3; its
	// stated uncertainty 0.05. The model: NBS's enthalpy and Gibbs energy of ionisation at 25 C, its heat
	// capacity held constant (docs/DATA_SOURCES.md)
	for (const auto [celsius, pkw] : { std::pair{ 0.0, 14.95 }, std::pair{ 25.0, 13.99 }, std::pair{ 50.0, 13.26 }, std::pair{ 75.0, 12.70 }, std::pair{ 100.0, 12.25 } }) {
		const double model = -std::log10(water_ionisation(builtin_species(), celsius + 273.15));
		INFO(celsius << " C: pKw " << model << " against " << pkw);
		CHECK(std::abs(model - pkw) < 0.06);
	}
	CHECK(std::abs(-std::log10(water_ionisation(builtin_species(), 298.15)) - 13.995) < 0.001);
}

TEST_CASE("measured: sodium hydroxide's and sodium sulfate's mean activity coefficients are Hamer and Wu's and Goldberg's (Stage 43)") {
	const SpeciesTable &t = builtin_species();
	// NaOH, a = (m gamma)^2: Hamer and Wu 1972, Table 19
	for (const auto [m, gamma] : { std::pair{ 0.5, 0.685 }, std::pair{ 1.0, 0.674 } }) {
		const double model = std::sqrt(activity(t, Cell().put(builtin::sodium_hydroxide_aq, m).state, builtin::sodium_hydroxide_aq)) / m;
		INFO("NaOH " << m << " mol/kg: " << model << " against " << gamma);
		CHECK(near_enough(model, gamma));
	}
	// Na2SO4, a = (2m)^2 m gamma^3: Goldberg 1981
	for (const auto [m, gamma] : { std::pair{ 0.1, 0.4457 }, std::pair{ 0.5, 0.2684 }, std::pair{ 1.0, 0.2040 } }) {
		const double model = std::cbrt(activity(t, Cell().put(builtin::sodium_sulfate_aq, m).state, builtin::sodium_sulfate_aq) / 4.0) / m;
		INFO("Na2SO4 " << m << " mol/kg: " << model << " against " << gamma);
		CHECK(near_enough(model, gamma));
	}
}

TEST_CASE("acidity: the neutralisation gives out 55.84 kJ for each water it makes, from the tables alone (Stage 43)") {
	const SpeciesTable &t = builtin_species();
	const auto hf = [&](SpeciesId s) { return t.species(s).hf; };
	const double heat = hf(builtin::sodium_sulfate_aq) + 2.0 * hf(builtin::water) - hf(builtin::sulfuric_acid_aq) - 2.0 * hf(builtin::sodium_hydroxide_aq);
	CHECK(heat == doctest::Approx(-111.672e3).epsilon(1e-5));
	// and at infinite dilution H+ + OH- -> H2O is the ionisation backwards (NBS: 55.836 kJ/mol)
	CHECK(-heat / 2.0 == doctest::Approx(t.solution().ionisation_h).epsilon(1e-4));
}

TEST_CASE("acidity: pure water is neutral at every temperature; an acid's pH is its hydrogen ions', a base's is set by Kw (Stage 43)") {
	const SpeciesTable &t = builtin_species();
	for (const double kelvin : { 273.15, 298.15, 323.15, 373.15 }) {
		CHECK(ph_of(Cell(), kelvin) == doctest::Approx(-0.5 * std::log10(water_ionisation(t, kelvin))).epsilon(1e-9));
	}
	CHECK(ph_of(Cell()) == doctest::Approx(6.998).epsilon(1e-3));
	// 0.01 mol/kg of the acid, fully dissociated: 0.02 mol/kg of H+ at its activity coefficient (0.69 by the
	// acid's fit: about 1.87 measured, where the bisulfate holds a share of it back)
	const double acid = ph_of(Cell().put(builtin::sulfuric_acid_aq, 0.01));
	CHECK(acid > 1.7);
	CHECK(acid < 1.95);
	// tenfold diluted, about one unit up
	const double dilute = ph_of(Cell().put(builtin::sulfuric_acid_aq, 0.001));
	CHECK(dilute - acid > 0.85);
	CHECK(dilute - acid < 1.05);
	// 0.01 mol/kg of the base: pH 12 less what its activity coefficients take
	const double base = ph_of(Cell().put(builtin::sodium_hydroxide_aq, 0.01));
	CHECK(base > 11.8);
	CHECK(base < 12.0);
	// the salt of the two is neutral
	CHECK(ph_of(Cell().put(builtin::sodium_sulfate_aq, 0.1)) == doctest::Approx(7.0).epsilon(0.01));
	// past the activity model's range, a base boiled nearly dry: no pH (Stage 44)
	CHECK(ph_of(Cell().put(builtin::sodium_hydroxide_aq, 5.9)) > 14.0);
	double none = 0.0;
	CHECK_FALSE(ph(t, Cell().put(builtin::sodium_hydroxide_aq, 6.1).state, 298.15, none));
	CHECK_FALSE(ph(t, Cell().put(builtin::sulfuric_acid_aq, 3.1).state, 298.15, none));
	// no liquid: no pH
	Cell dry;
	dry.state.amount[static_cast<size_t>(builtin::water.index)] = 0;
	double p = 0.0;
	CHECK_FALSE(ph(t, dry.state, 298.15, p));
}

TEST_CASE("acidity: an acid and a base mixed neutralise in the step, the solution warming by the closed form; every invariant holds (Stage 43)") {
	const SpeciesTable &table = builtin_species();
	bool ok = false;
	for (const double base : { 0.04, 0.05, 0.06 }) { // short of the acid's 0.05 mol of H+, at it, past it
		World::Builder b(table);
		const CellId beaker = b.add_cell("beaker");
		const double water = 75.0 / 18.015;
		b.set_amount(beaker, builtin::water, amount_from_mol(water, ok)).set_amount(beaker, builtin::sulfuric_acid_aq, amount_from_mol(0.025, ok));
		b.set_amount(beaker, builtin::sodium_hydroxide_aq, amount_from_mol(base, ok));
		b.set_temperature(beaker, 293.15);
		std::string why;
		auto world = b.build(&why);
		REQUIRE_MESSAGE(world, why);
		Simulation sim(std::move(*world), std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end()));
		sim.set_checking(true);
		const Totals genesis = totals(table, sim.world().snapshot());
		const StepTrace trace = sim.step();
		const CellState &end = sim.world().snapshot_cell(beaker);
		CHECK(totals(table, sim.world().snapshot()) == genesis);
		std::vector<Reaction> equilibria;
		for (const Reaction &r : builtin_reactions()) {
			if (r.kinetics().law == Kinetics::Law::Equilibrium) {
				equilibria.push_back(r);
			}
		}
		const Violations violations = check_all(table, trace, equilibria); // P1 among them: the cell at its fast equilibria
		CHECK_MESSAGE(violations.empty(), (violations.empty() ? std::string() : violations.front()));
		// one of the two is used up, to what water's own ionisation leaves of it (at the equivalence, both: a
		// hundred-millionth of a mole of each, the 1e-7 mol/kg of neutral water)
		const auto mol = [&](SpeciesId s) { return static_cast<double>(end.amount[static_cast<size_t>(s.index)]) * MOL_PER_QUANTUM; };
		const double made = std::min(0.025, base / 2.0);
		CHECK(mol(builtin::sodium_sulfate_aq) == doctest::Approx(made).epsilon(1e-6));
		CHECK(std::min(mol(builtin::sulfuric_acid_aq), mol(builtin::sodium_hydroxide_aq)) < (base == 0.05 ? 1e-7 : 1e-11));
		INFO("left: acid " << mol(builtin::sulfuric_acid_aq) << " mol, base " << mol(builtin::sodium_hydroxide_aq) << " mol");
		// the closed form: the heat of the waters made, at 20 C, over what the beaker holds now
		const auto cp = [&](SpeciesId s) { return table.species(s).cp; };
		const auto hf = [&](SpeciesId s) { return table.species(s).hf; };
		const double reaction_heat = hf(builtin::sodium_sulfate_aq) + 2.0 * hf(builtin::water) - hf(builtin::sulfuric_acid_aq) - 2.0 * hf(builtin::sodium_hydroxide_aq) +
				(cp(builtin::sodium_sulfate_aq) + 2.0 * cp(builtin::water) - cp(builtin::sulfuric_acid_aq) - 2.0 * cp(builtin::sodium_hydroxide_aq)) * (293.15 - T0);
		double before = 0.0, after = 0.0;
		REQUIRE(sim.world().temperature(beaker, after));
		before = 293.15;
		const double capacity = thermal_state(table, end).heat_capacity;
		CHECK(after - before == doctest::Approx(-made * reaction_heat / capacity).epsilon(0.01));
		INFO("base " << base << " mol: " << after - before << " K");
		// the pH: acid left, neutral, base left
		double p = 0.0;
		REQUIRE(ph(table, end, after, p));
		if (base < 0.05) {
			CHECK(p < 2.0);
		} else if (base > 0.05) {
			CHECK(p > 12.0);
		} else {
			CHECK(std::abs(p - 7.0) < 0.2);
		}
	}
}
