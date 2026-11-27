// Weak acids and bases (GAME_SPEC §9, Stage 64): acetic acid, ammonia, carbonic and phosphoric acid against their
// sources' constants; the IUPAC pH standards; the chlorides' activity against Hamer and Wu; carbon dioxide and ammonia
// between a solution and its gas (Henry's law); salts read back by their bases.
#include "tables.hpp"
#include "chem/builtin_ids.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <initializer_list>
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

double ph_of(const Cell &c, double kelvin = 298.15) {
	double p = 0.0;
	REQUIRE(ph(builtin_species(), testing::settled(c.state, kelvin), kelvin, p));
	return p;
}

// a reaction's log10 K from the tables at temperature T, each species' G its formation enthalpy and entropy carried by
// its constant heat capacity (as the model's affinity has it)
double log_k(std::initializer_list<std::pair<SpeciesId, int>> terms, double kelvin) {
	double g = 0.0;
	for (const auto &[s, nu] : terms) {
		const SpeciesDef &d = builtin_species().species(s);
		g += nu * (d.hf + d.cp * (kelvin - T0) - kelvin * (d.s0 + d.cp * std::log(kelvin / T0)));
	}
	return -g / (GAS_CONSTANT * kelvin * std::log(10.0));
}

double reaction_heat(std::initializer_list<std::pair<SpeciesId, int>> terms) {
	double h = 0.0;
	for (const auto &[s, nu] : terms) {
		h += nu * builtin_species().species(s).hf;
	}
	return h;
}

// PHREEQC's analytic log K = A1 + A2 T + A3 / T + A4 log10 T + A5 / T^2 + A6 T^2
double analytic(std::initializer_list<double> a, double kelvin) {
	double c[6] = { 0, 0, 0, 0, 0, 0 };
	int i = 0;
	for (const double v : a) {
		c[i++] = v;
	}
	return c[0] + c[1] * kelvin + c[2] / kelvin + c[3] * std::log10(kelvin) + c[4] / (kelvin * kelvin) + c[5] * kelvin * kelvin;
}

double mol_of(const CellState &c, SpeciesId s) {
	return static_cast<double>(c.amount[static_cast<size_t>(s.index)]) * MOL_PER_QUANTUM;
}

} // namespace

TEST_CASE("measured: ammonia's, carbonic acid's and the gases' constants are PHREEQC's from 0 to 60 C (Stage 64)") {
	using namespace builtin;
	const int water_ = 0; // (a marker: water's terms below are builtin::water)
	(void)water_;
	for (const double celsius : { 0.0, 25.0, 40.0, 60.0 }) {
		const double kelvin = celsius + 273.15;
		INFO(celsius << " C");
		// phreeqc.dat: NH4+ = NH3 + H+, log K -9.252, 12.48 kcal; its analytic 0.6322 -0.001225 -2835.76
		const double nh4 = log_k({ { ammonium_ion, -1 }, { ammonia_aq, 1 }, { hydrogen_ion, 1 } }, kelvin);
		CHECK(std::abs(nh4 - analytic({ 0.6322, -0.001225, -2835.76 }, kelvin)) < 0.02);
		// CO3-2 + H+ = HCO3-, and CO3-2 + 2 H+ = CO2 + H2O (log K 10.329 and 16.681): K1 and K2 of carbonic acid
		const double hco3 = analytic({ 107.8871, 0.03252849, -5151.79, -38.92561, 563713.9 }, kelvin);
		const double co2 = analytic({ 464.1965, 0.09344813, -26986.16, -165.75951, 2248628.9 }, kelvin);
		const double k1 = log_k({ { carbon_dioxide_aq, -1 }, { water, -1 }, { bicarbonate_ion, 1 }, { hydrogen_ion, 1 } }, kelvin);
		const double k2 = log_k({ { bicarbonate_ion, -1 }, { carbonate_ion, 1 }, { hydrogen_ion, 1 } }, kelvin);
		MESSAGE(celsius << " C: pKa1 " << -k1 << " against " << co2 - hco3 << "; pKa2 " << -k2 << " against " << hco3 << "; pKa(NH4+) " << -nh4);
		CHECK(std::abs(-k1 - (co2 - hco3)) < 0.05); // the heat capacities are Shock and Helgeson's, PHREEQC's fit its own
		CHECK(std::abs(-k2 - hco3) < 0.02);
		// the gases: CO2(g) = CO2, log K -1.468; NH3(g) = NH3, 1.7966 (NBS's own 1.761)
		const double henry_co2 = log_k({ { carbon_dioxide, -1 }, { carbon_dioxide_aq, 1 } }, kelvin);
		const double henry_nh3 = log_k({ { ammonia, -1 }, { ammonia_aq, 1 } }, kelvin);
		CHECK(std::abs(henry_co2 - analytic({ 10.5624, -2.3547e-2, -3972.8, 0, 5.8746e5, 1.9194e-5 }, kelvin)) < 0.02);
		// (NBS's rows, which the tables take, give 1.761 at 25 C against phreeqc.dat's 1.7966, wateq4f.dat's 1.77: the
		// sources' own spread; their heat capacities Shock and Helgeson's)
		CHECK(std::abs(henry_nh3 - analytic({ -18.758, 3.367e-4, 2.5113e3, 4.8619, 39.192 }, kelvin)) < 0.08);
	}
	// the heats at 25 C: NH4+'s dissociation 52.22 kJ (12.48 kcal), HCO3-'s -14.90 (-3.561 kcal), CO2's -24.01 (-5.738)
	CHECK(reaction_heat({ { ammonium_ion, -1 }, { ammonia_aq, 1 }, { hydrogen_ion, 1 } }) == doctest::Approx(12.48 * 4184.0).epsilon(0.005));
	CHECK(reaction_heat({ { carbonate_ion, -1 }, { hydrogen_ion, -1 }, { bicarbonate_ion, 1 } }) == doctest::Approx(-3.561 * 4184.0).epsilon(0.01));
	CHECK(std::abs(reaction_heat({ { carbonate_ion, -1 }, { hydrogen_ion, -2 }, { carbon_dioxide_aq, 1 }, { water, 1 } }) - -5.738 * 4184.0) < 1600.0);
}

TEST_CASE("measured: acetic acid's and phosphoric acid's constants are MINTEQ's (NIST 46) (Stage 64)") {
	using namespace builtin;
	// minteq.v4.dat: H+ + Acetate- = H(Acetate), log K 4.757, 0.41 kJ
	CHECK(std::abs(-log_k({ { acetic_acid_aq, -1 }, { acetate_ion, 1 }, { hydrogen_ion, 1 } }, 298.15) - 4.757) < 0.015);
	CHECK(std::abs(reaction_heat({ { acetic_acid_aq, -1 }, { acetate_ion, 1 }, { hydrogen_ion, 1 } }) - -0.41e3) < 300.0);
	// its dissociation's heat capacity, Shock 1995's (Harned and Ehlers measured near -150): a pKa that rises slowly
	// either side of 25 C, within 0.06 of 4.757 from 0 to 60 C
	for (const double celsius : { 0.0, 60.0 }) {
		const double pka = -log_k({ { acetic_acid_aq, -1 }, { acetate_ion, 1 }, { hydrogen_ion, 1 } }, celsius + 273.15);
		CHECK(pka > 4.757);
		CHECK(pka < 4.757 + 0.08);
	}
	// PO4-3 + H+ = HPO4-2 12.375 (-15 kJ); + 2 H+ = H2PO4- 19.573 (-18 kJ); + 3 H+ = H3PO4 21.721 (-10.1 kJ)
	const double pka3 = -log_k({ { hydrogen_phosphate_ion, -1 }, { phosphate_ion, 1 }, { hydrogen_ion, 1 } }, 298.15);
	const double pka2 = -log_k({ { dihydrogen_phosphate_ion, -1 }, { hydrogen_phosphate_ion, 1 }, { hydrogen_ion, 1 } }, 298.15);
	const double pka1 = -log_k({ { phosphoric_acid_aq, -1 }, { dihydrogen_phosphate_ion, 1 }, { hydrogen_ion, 1 } }, 298.15);
	MESSAGE("phosphoric acid: pKa " << pka1 << ", " << pka2 << ", " << pka3);
	CHECK(std::abs(pka1 - (21.721 - 19.573)) < 0.01);
	CHECK(std::abs(pka2 - (19.573 - 12.375)) < 0.01);
	CHECK(std::abs(pka3 - 12.375) < 0.04); // phreeqc.dat 12.346, llnl.dat 12.322: the sources' own spread
	CHECK(std::abs(reaction_heat({ { phosphate_ion, -1 }, { hydrogen_ion, -1 }, { hydrogen_phosphate_ion, 1 } }) - -15e3) < 1000.0);
	CHECK(std::abs(reaction_heat({ { phosphate_ion, -1 }, { hydrogen_ion, -2 }, { dihydrogen_phosphate_ion, 1 } }) - -18e3) < 1000.0);
	CHECK(std::abs(reaction_heat({ { phosphate_ion, -1 }, { hydrogen_ion, -3 }, { phosphoric_acid_aq, 1 } }) - -10.1e3) < 1000.0);
}

TEST_CASE("measured: water's ionisation from the tables has its measured heat capacity (Stage 64)") {
	// (Stage 64) H+ at -216.35 J/K mol, the ions' shift: H2O -> H+ + OH- then has NBS's -223.8 (it had -7.5), so the
	// tables' own pKw follows IAPWS R11-24 (Table 3) as the pH's measured one does
	using namespace builtin;
	const SpeciesTable &t = builtin_species();
	CHECK(t.species(hydrogen_ion).cp + t.species(hydroxide_ion).cp - t.species(water).cp == doctest::Approx(-223.8).epsilon(0.002));
	for (const auto [celsius, pkw] : { std::pair{ 0.0, 14.95 }, std::pair{ 50.0, 13.26 }, std::pair{ 100.0, 12.25 } }) {
		const double model = -log_k({ { water, -1 }, { hydrogen_ion, 1 }, { hydroxide_ion, 1 } }, celsius + 273.15);
		INFO(celsius << " C: pKw " << model << " against " << pkw);
		CHECK(std::abs(model - pkw) < 0.07); // its heat capacity held at 25 C's (IAPWS's falls with temperature)
	}
}

TEST_CASE("measured: hydrochloric acid's, sodium chloride's and potassium chloride's mean activity coefficients are Hamer and Wu's (Stage 64)") {
	const SpeciesTable &t = builtin_species();
	// Hamer and Wu 1972 (J. Phys. Chem. Ref. Data 1, 1047): Tables 4, 16 and 28, gamma at 25 C
	const struct {
		SpeciesId unit;
		double m, gamma;
	} rows[] = {
		{ builtin::hydrochloric_acid_aq, 0.1, 0.797 },
		{ builtin::hydrochloric_acid_aq, 1.0, 0.811 },
		{ builtin::hydrochloric_acid_aq, 3.0, 1.316 },
		{ builtin::sodium_chloride_aq, 0.1, 0.779 },
		{ builtin::sodium_chloride_aq, 1.0, 0.657 },
		{ builtin::sodium_chloride_aq, 3.0, 0.714 },
		{ builtin::sodium_chloride_aq, 6.0, 0.986 },
		{ builtin::potassium_chloride_aq, 0.1, 0.768 },
		{ builtin::potassium_chloride_aq, 1.0, 0.604 },
		{ builtin::potassium_chloride_aq, 4.0, 0.576 },
	};
	for (const auto &row : rows) {
		const double model = std::sqrt(activity(t, Cell().put(row.unit, row.m).state, row.unit)) / row.m;
		INFO(t.species(row.unit).id << " " << row.m << " mol/kg: " << model << " against " << row.gamma);
		CHECK(std::abs(model / row.gamma - 1.0) < 0.02);
	}
}

TEST_CASE("measured: the IUPAC primary pH standards of phosphate and carbonate read their values at 25 C (Stage 64)") {
	// Buck et al. 2002 (Pure Appl. Chem. 74, 2169), Table 2, the Bates-Guggenheim convention (its stated 0.01 at 95 %);
	// the model's pH is -log10 of the hydrogen ion's activity by Pitzer's coefficients, a convention of its own
	using namespace builtin;
	const double phosphate = ph_of(Cell().put(potassium_dihydrogen_phosphate_aq, 0.025).put(disodium_hydrogen_phosphate_aq, 0.025));
	const double carbonate = ph_of(Cell().put(sodium_bicarbonate_aq, 0.025).put(sodium_carbonate_aq, 0.025));
	MESSAGE("phosphate 0.025 + 0.025: pH " << phosphate << " (6.865); carbonate 0.025 + 0.025: pH " << carbonate << " (10.012)");
	CHECK(std::abs(phosphate - 6.865) < 0.03);
	CHECK(std::abs(carbonate - 10.012) < 0.03);
	for (const auto [celsius, value] : { std::pair{ 10.0, 6.923 }, std::pair{ 40.0, 6.838 } }) {
		const double p = ph_of(Cell().put(potassium_dihydrogen_phosphate_aq, 0.025).put(disodium_hydrogen_phosphate_aq, 0.025), celsius + 273.15);
		INFO(celsius << " C: phosphate pH " << p << " against " << value);
		CHECK(std::abs(p - value) < 0.04);
	}
	for (const auto [celsius, value] : { std::pair{ 10.0, 10.179 }, std::pair{ 40.0, 9.889 } }) {
		const double p = ph_of(Cell().put(sodium_bicarbonate_aq, 0.025).put(sodium_carbonate_aq, 0.025), celsius + 273.15);
		INFO(celsius << " C: carbonate pH " << p << " against " << value);
		CHECK(std::abs(p - value) < 0.04);
	}
}

TEST_CASE("acidity: a weak acid, its buffer, a weak base and their salts, by the closed forms (Stage 64)") {
	using namespace builtin;
	const double pka = -log_k({ { acetic_acid_aq, -1 }, { acetate_ion, 1 }, { hydrogen_ion, 1 } }, 298.15);
	// 0.1 mol/kg acetic acid: a hundredth of it dissociated, pH (pKa - log 0.1) / 2 to within its activity's share
	const double acid = ph_of(Cell().put(acetic_acid_aq, 0.1));
	MESSAGE("acetic acid 0.1 mol/kg: pH " << acid << ", the ideal closed form " << 0.5 * (pka + 1.0));
	CHECK(std::abs(acid - 0.5 * (pka + 1.0)) < 0.03);
	// the acid with as much of its salt: Henderson and Hasselbalch, pH = pKa, less the activity coefficient's log
	const double buffer = ph_of(Cell().put(acetic_acid_aq, 0.1).put(sodium_acetate_aq, 0.1));
	MESSAGE("acetic acid + sodium acetate, 0.1 + 0.1 mol/kg: pH " << buffer);
	CHECK(buffer < pka);
	CHECK(buffer > pka - 0.15);
	// a buffer holds: a hundredth of a mole of the strong base moves it by about log(0.11/0.09) = 0.087
	const double pushed = ph_of(Cell().put(acetic_acid_aq, 0.1).put(sodium_acetate_aq, 0.1).put(sodium_hydroxide_aq, 0.01));
	CHECK(pushed - buffer == doctest::Approx(std::log10(0.11 / 0.09)).epsilon(0.05));
	// the salts: sodium acetate basic, ammonium chloride acid, sodium chloride neutral
	const double acetate = ph_of(Cell().put(sodium_acetate_aq, 0.1));
	const double ammonium = ph_of(Cell().put(ammonium_chloride_aq, 0.1));
	const double chloride = ph_of(Cell().put(sodium_chloride_aq, 0.1));
	MESSAGE("0.1 mol/kg: sodium acetate pH " << acetate << ", ammonium chloride " << ammonium << ", sodium chloride " << chloride);
	// (the ideal closed forms: the ions' activity coefficients, about 0.78 at 0.1 mol/kg, move each by up to 0.12)
	CHECK(std::abs(acetate - 0.5 * (14.0 + pka - 1.0)) < 0.15);
	CHECK(std::abs(ammonium - 0.5 * (9.25 + 1.0)) < 0.15);
	CHECK(acetate > 8.5);
	CHECK(ammonium < 5.5);
	CHECK(std::abs(chloride - 7.0) < 0.05);
	// ammonia 0.1 mol/kg: pOH (pKb + 1) / 2
	const double ammonia_ph = ph_of(Cell().put(ammonia_aq, 0.1));
	MESSAGE("ammonia 0.1 mol/kg: pH " << ammonia_ph);
	CHECK(ammonia_ph == doctest::Approx(14.0 - 0.5 * (14.0 - 9.25 + 1.0)).epsilon(0.01));
	// a salt read back by its bases: the acid neutralised to its salt reads as sodium acetate, and no acid is left
	const CellState made = testing::settled(Cell().put(acetic_acid_aq, 0.1).put(sodium_hydroxide_aq, 0.1).state, 298.15);
	std::vector<std::pair<SpeciesId, double>> dissolved;
	for (int k = 0; k < builtin_species().species_count(); ++k) {
		if (builtin_species().species(SpeciesId{ k }).phase == Phase::Aqueous && made.amount[static_cast<size_t>(k)] > 0) {
			dissolved.emplace_back(SpeciesId{ k }, mol_of(made, SpeciesId{ k }));
		}
	}
	CHECK(formula_units(builtin_species(), dissolved, sodium_acetate_aq) == doctest::Approx(0.1).epsilon(1e-6));
	CHECK(formula_units(builtin_species(), dissolved, sodium_hydroxide_aq) < 1e-4);
	CHECK(formula_units(builtin_species(), dissolved, sodium_carbonate_aq) == 0.0);
}

TEST_CASE("acidity: carbon dioxide and ammonia trade with the gas over their solution by Henry's law; a solution over-full of the gas bubbles (Stage 64)") {
	using namespace builtin;
	const SpeciesTable &table = builtin_species();
	bool ok = false;
	for (const bool co2 : { true, false }) {
		World::Builder b(table);
		const CellId room = b.add_cell("room");
		const CellId flask = b.add_rigid_cell("flask", 1e-3); // a litre, by no gas path to the room: a closed flask
		b.set_amount(room, nitrogen, amount_from_mol(40.0, ok));
		b.set_amount(flask, water, amount_from_mol(0.5 / 0.018015, ok)); // half a litre of water, half a litre of air
		b.set_amount(flask, nitrogen, amount_from_mol(0.02, ok));
		b.set_amount(flask, co2 ? carbon_dioxide : ammonia, amount_from_mol(0.01, ok));
		b.set_temperature(room, 298.15).set_temperature(flask, 298.15);
		std::string why;
		auto world = build_settled(b, builtin_reactions(), &why);
		REQUIRE_MESSAGE(world, why);
		const CellState &c = world->snapshot_cell(flask);
		const SpeciesId gas = co2 ? carbon_dioxide : ammonia, solute = co2 ? carbon_dioxide_aq : ammonia_aq;
		double gas_mol = 0.0;
		for (int k = 0; k < table.species_count(); ++k) {
			if (table.species(SpeciesId{ k }).phase == Phase::Gas) {
				gas_mol += mol_of(c, SpeciesId{ k });
			}
		}
		const double a_gas = activity(table, c, gas, &world->meta(flask));
		const double a_aq = activity(table, c, solute, &world->meta(flask));
		const double k = std::pow(10.0, log_k({ { gas, -1 }, { solute, 1 } }, 298.15));
		INFO((co2 ? "CO2" : "NH3") << ": dissolved " << mol_of(c, solute) << " mol, gas " << mol_of(c, gas) << " mol of " << gas_mol);
		CHECK(mol_of(c, solute) > 0.0);
		CHECK(mol_of(c, gas) > 0.0);
		CHECK(a_aq / a_gas == doctest::Approx(k).epsilon(0.01));
		if (!co2) {
			CHECK(mol_of(c, solute) + mol_of(c, ammonium_ion) > 0.95 * 0.01); // ammonia goes nearly all into the water
		}
	}
	// a kilogram of water holding a tenth of a mole of CO2 (three times what a pure atmosphere of it dissolves), with no
	// gas over it: it gives off a bubble of CO2 until what is left is what 1 atm of it holds
	Cell over;
	over.put(carbon_dioxide_aq, 0.1);
	const CellState settled = testing::settled(over.state, 298.15);
	const double kh = std::pow(10.0, log_k({ { carbon_dioxide, -1 }, { carbon_dioxide_aq, 1 } }, 298.15));
	MESSAGE("CO2 over-full: " << mol_of(settled, carbon_dioxide_aq) << " mol/kg left, " << mol_of(settled, carbon_dioxide) << " mol given off");
	CHECK(mol_of(settled, carbon_dioxide) > 0.05);
	CHECK(mol_of(settled, carbon_dioxide_aq) < 1.1 * kh);
	// under it: no bubble
	Cell under;
	under.put(carbon_dioxide_aq, 0.01);
	CHECK(mol_of(testing::settled(under.state, 298.15), carbon_dioxide) == 0.0);
}
