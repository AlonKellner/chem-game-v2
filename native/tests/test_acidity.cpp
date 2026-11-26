// Acids, bases and pH (GAME_SPEC §9, Stage 43): water's ionisation against IAPWS, the base's and its salt's
// activity against their evaluated tables, the neutralisation's heat from the tables, and the pH.
#include "tables.hpp"
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
		testing::put_amount(builtin_species(), state, s, quanta(mol));
		return *this;
	}
};

// within the 2.5 % the Pitzer parameters keep to the tables (Pitzer and Mayorga's own fit is to about 2 %)
bool near_enough(double model, double measured) {
	return std::abs(model / measured - 1.0) < 0.025;
}

// what is put in the cell, taken to its fast equilibria at the temperature
CellState settled(const Cell &c, double kelvin) {
	return testing::settled(c.state, kelvin);
}

double ph_of(const Cell &c, double kelvin = 298.15) {
	double p = 0.0;
	REQUIRE(ph(builtin_species(), settled(c, kelvin), kelvin, p));
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
	// in ions (Stage 63), the neutralisation itself: the same heat, the ions' data the formula units' split exactly
	CHECK(hf(builtin::water) - hf(builtin::hydrogen_ion) - hf(builtin::hydroxide_ion) == doctest::Approx(heat / 2.0).epsilon(1e-9));
	CHECK(hf(builtin::sodium_sulfate_aq) == doctest::Approx(2.0 * hf(builtin::sodium_ion) + hf(builtin::sulfate_ion)).epsilon(1e-12));
}

TEST_CASE("acidity: pure water is neutral at every temperature; an acid's pH is its hydrogen ions', a base's is set by Kw (Stage 43)") {
	const SpeciesTable &t = builtin_species();
	for (const double kelvin : { 273.15, 298.15, 323.15, 373.15 }) {
		// (to a millionth: its own ions, 1e-7 mol/kg of each, take their activity coefficients a hair under one)
		CHECK(ph_of(Cell(), kelvin) == doctest::Approx(-0.5 * std::log10(water_ionisation(t, kelvin))).epsilon(1e-6));
	}
	CHECK(ph_of(Cell()) == doctest::Approx(6.998).epsilon(1e-3));
	// 0.01 mol/kg of the acid: its second hydrogen held back in part as bisulfate (Stage 63), about 1.87 measured
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
	// the salt of the two is all but neutral: sulfate is the bisulfate's base (Stage 63: Kb = Kw / K2, about 1e-12), a
	// little over 7
	const double salt = ph_of(Cell().put(builtin::sodium_sulfate_aq, 0.1));
	CHECK(salt > 7.05);
	CHECK(salt < 7.5);
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
		// hundred-millionth of a mole of each, the 1e-7 mol/kg of neutral water); the acid left is free hydrogen ions
		// and bisulfate (Stage 63)
		const auto mol = [&](SpeciesId s) { return static_cast<double>(end.amount[static_cast<size_t>(s.index)]) * MOL_PER_QUANTUM; };
		const double acid_left = mol(builtin::hydrogen_ion) + mol(builtin::hydrogen_sulfate_ion);
		const double base_left = mol(builtin::hydroxide_ion);
		CHECK(acid_left - base_left == doctest::Approx(0.05 - base).epsilon(1e-6));
		CHECK(std::min(acid_left, base_left) < (base == 0.05 ? 1e-7 : 1e-11));
		INFO("left: acid " << acid_left << " mol, base " << base_left << " mol");
		// the closed form: the heat of what the step made and used, at 20 C, over what the beaker holds now
		const CellState &start = trace.before.cells[static_cast<size_t>(beaker.index)];
		double reaction_heat = 0.0;
		for (size_t s = 0; s < end.amount.size(); ++s) {
			const SpeciesDef &d = table.species(SpeciesId{ static_cast<int32_t>(s) });
			if (d.phase != Phase::Gas) {
				reaction_heat += static_cast<double>(end.amount[s] - start.amount[s]) * MOL_PER_QUANTUM * (d.hf + d.cp * (293.15 - T0));
			}
		}
		const double made = std::min(0.05, base);
		CHECK(reaction_heat < -0.9 * made * 55.836e3); // the waters' heat, less the bisulfate's in the acid left
		double before = 0.0, after = 0.0;
		REQUIRE(sim.world().temperature(beaker, after));
		before = 293.15;
		const double capacity = thermal_state(table, end).heat_capacity;
		CHECK(after - before == doctest::Approx(-reaction_heat / capacity).epsilon(0.01));
		INFO("base " << base << " mol: " << after - before << " K");
		// the pH: acid left, neutral, base left
		double p = 0.0;
		REQUIRE(ph(table, end, after, p));
		if (base < 0.05) {
			CHECK(p < 2.0);
		} else if (base > 0.05) {
			CHECK(p > 12.0);
		} else {
			CHECK(p > 7.0); // the sodium sulfate made: sulfate a weak base (Stage 63)
			CHECK(p < 7.5);
		}
	}
}


namespace {

// a reaction's equilibrium constant from the tables at temperature T: K = exp(-dG / RT), each species' G its
// formation enthalpy and entropy carried by its constant heat capacity (as the model's affinity has it)
double constant_of(std::initializer_list<std::pair<SpeciesId, int>> terms, double kelvin) {
	double g = 0.0;
	for (const auto &[s, nu] : terms) {
		const SpeciesDef &d = builtin_species().species(s);
		g += nu * (d.hf + d.cp * (kelvin - T0) - kelvin * (d.s0 + d.cp * std::log(kelvin / T0)));
	}
	return std::exp(-g / (GAS_CONSTANT * kelvin));
}

} // namespace

TEST_CASE("measured: the bisulfate's dissociation constant is PHREEQC's (pitzer.dat) from 0 to 100 C (Stage 63)") {
	// pitzer.dat (PHREEQC v3, USGS): SO4-2 + H+ = HSO4-, log K 1.988 and 3.85 kcal at 25 C, and its analytic
	// log K = -56.889 + 0.006473 T + 2307.9 / T + 19.8858 log10 T; the model's heat capacity held at its 25 C slope
	for (const double celsius : { 0.0, 25.0, 50.0, 75.0, 100.0 }) {
		const double kelvin = celsius + 273.15;
		const double model = std::log10(constant_of({ { builtin::hydrogen_sulfate_ion, -1 }, { builtin::hydrogen_ion, 1 }, { builtin::sulfate_ion, 1 } }, kelvin));
		const double source = -(-56.889 + 0.006473 * kelvin + 2307.9 / kelvin + 19.8858 * std::log10(kelvin));
		INFO(celsius << " C: log K2 " << model << " against " << source);
		CHECK(std::abs(model - source) < (celsius == 25.0 ? 0.001 : 0.02));
	}
	const SpeciesTable &t = builtin_species();
	const double heat = t.species(builtin::hydrogen_ion).hf + t.species(builtin::sulfate_ion).hf - t.species(builtin::hydrogen_sulfate_ion).hf;
	CHECK(heat == doctest::Approx(-3.85 * 4184.0).epsilon(1e-4));
}

TEST_CASE("measured: copper's first hydrolysis constant and heat are MINTEQ's (Stage 63)") {
	// minteq.v4.dat (PHREEQC v3, USGS): Cu+2 + H2O = CuOH+ + H+, log K -7.497, 35.81 kJ; no heat capacity of its own
	// is given (the model's holds it at nought)
	const SpeciesTable &t = builtin_species();
	const auto terms = { std::pair{ builtin::copper_ion, -1 }, std::pair{ builtin::water, -1 }, std::pair{ builtin::copper_hydroxide_ion, 1 }, std::pair{ builtin::hydrogen_ion, 1 } };
	CHECK(std::log10(constant_of(terms, 298.15)) == doctest::Approx(-7.497).epsilon(1e-4));
	const double heat = t.species(builtin::copper_hydroxide_ion).hf + t.species(builtin::hydrogen_ion).hf - t.species(builtin::copper_ion).hf - t.species(builtin::water).hf;
	CHECK(heat == doctest::Approx(35.81e3).epsilon(1e-4));
	// and, the copper sulfate solution it makes acid: 0.5 mol/kg reads under 5 (measured about 4; the dimer is left
	// out, GAME_SPEC §20)
	const double p = ph_of(Cell().put(builtin::copper_sulfate_aq, 0.5));
	MESSAGE("copper sulfate 0.5 mol/kg: pH " << p);
	CHECK(p > 4.0);
	CHECK(p < 5.0);
}

TEST_CASE("acidity: a retired formula unit is read as what its ions make, by each element and the balance of protons (Stage 63)") {
	const SpeciesTable &t = builtin_species();
	const auto units = [&](const CellState &c, SpeciesId unit) {
		std::vector<std::pair<SpeciesId, double>> dissolved;
		for (int k = 0; k < t.species_count(); ++k) {
			if (t.species(SpeciesId{ k }).phase == Phase::Aqueous && c.amount[static_cast<size_t>(k)] > 0) {
				dissolved.emplace_back(SpeciesId{ k }, static_cast<double>(c.amount[static_cast<size_t>(k)]) * MOL_PER_QUANTUM);
			}
		}
		return formula_units(t, dissolved, unit);
	};
	// the acid at its bisulfate: every hydrogen and sulfate of it, free or held
	const CellState acid = settled(Cell().put(builtin::sulfuric_acid_aq, 0.2), 298.15);
	REQUIRE(acid.amount[static_cast<size_t>(builtin::hydrogen_sulfate_ion.index)] > 0);
	CHECK(units(acid, builtin::sulfuric_acid_aq) == doctest::Approx(0.2).epsilon(1e-9));
	CHECK(units(acid, builtin::sodium_hydroxide_aq) == 0.0);
	// a base past the acid: no acid, the base what is left over
	const CellState base = settled(Cell().put(builtin::sulfuric_acid_aq, 0.1).put(builtin::sodium_hydroxide_aq, 0.3), 298.15);
	CHECK(units(base, builtin::sulfuric_acid_aq) == 0.0);
	CHECK(units(base, builtin::sodium_hydroxide_aq) == doctest::Approx(0.1).epsilon(1e-6));
	CHECK(units(base, builtin::sodium_sulfate_aq) == doctest::Approx(0.1).epsilon(1e-9));
	// copper sulfate at its hydrolysis, in acid: the copper in any of its ions
	const CellState copper = settled(Cell().put(builtin::copper_sulfate_aq, 0.5).put(builtin::sulfuric_acid_aq, 0.05), 298.15);
	CHECK(units(copper, builtin::copper_sulfate_aq) == doctest::Approx(0.5).epsilon(1e-9));
	CHECK(units(copper, builtin::sulfuric_acid_aq) == doctest::Approx(0.05).epsilon(1e-6));
	// what is not retired reads nothing here
	CHECK(units(copper, builtin::copper_ion) == 0.0);
}
