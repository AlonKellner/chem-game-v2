// Gas exchanged by diffusion at an open mouth (GAME_SPEC §6, Stage 50): each gas down its mole fraction's difference
// at its own diffusion in air (docs/DATA_SOURCES.md, "Gases' diffusion in air (Stage 50)"), held to the closed forms of
// a two-cell relaxation and of Stefan's evaporation; E1.
#include "chem/builtin_ids.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"
#include "tables.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace chem;

namespace {

constexpr double FLASK = 250e-6; // m^3
constexpr double MOUTH = 1.0e-2; // m: the exchange's area over length (a mouth of 3 cm^2 over 3 cm)

Amount mol(double n) {
	bool ok = false;
	const Amount a = amount_from_mol(n, ok);
	REQUIRE(ok);
	return a;
}

double fills(double volume, double kelvin) {
	return P_ATMOSPHERE * volume / (GAS_CONSTANT * kelvin);
}

double mol_of(const World &w, CellId c, SpeciesId s) {
	return w.amount(c, s).mol();
}

double gas_mol(const World &world, CellId cell) {
	double total = 0.0;
	for (int s = 0; s < world.species().species_count(); ++s) {
		if (world.species().species(SpeciesId{ s }).phase == Phase::Gas) {
			total += world.amount(cell, SpeciesId{ s }).mol();
		}
	}
	return total;
}

// A flask open to the room's air through a mouth that exchanges gas, both held at 20 C; `co2` mol of carbon dioxide
// in the flask's air, `water` mol of liquid water at its floor
struct Open {
	CellId flask, air;
	EdgeId mouth;
	Simulation sim;
};
Open open_flask(double co2, double water) {
	World::Builder b(builtin_species());
	const CellId flask = b.add_rigid_cell("flask", FLASK);
	const CellId air = b.add_cell("air");
	const CellId room = b.add_reservoir("room", 293.15);
	const EdgeId mouth = b.add_edge(flask, air, PHASES_GAS);
	b.add_edge(flask, room, PHASES_NONE, 1000.0); // held at the room's temperature
	b.add_edge(air, room, PHASES_NONE, 1000.0);
	const double condensed = water * 18.015 / 0.997048 * 1e-6, gas = fills(FLASK - condensed, 293.15) - co2;
	b.set_amount(flask, builtin::nitrogen, mol(0.79 * gas)).set_amount(flask, builtin::oxygen, mol(0.21 * gas));
	if (co2 > 0.0) {
		b.set_amount(flask, builtin::carbon_dioxide, mol(co2));
	}
	if (water > 0.0) {
		b.set_amount(flask, builtin::water, mol(water));
	}
	b.set_amount(flask, builtin::glass, mol(1.0)).set_temperature(flask, 293.15);
	b.set_amount(air, builtin::nitrogen, mol(79.0)).set_amount(air, builtin::oxygen, mol(21.0)).set_temperature(air, 293.15);
	std::string why;
	auto world = b.build(&why);
	REQUIRE_MESSAGE(world.has_value(), why);
	REQUIRE(world->set_exchange(mouth, MOUTH) == Refusal::None);
	Simulation sim(std::move(*world), std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end()));
	sim.set_checking(true);
	return Open{ flask, air, mouth, std::move(sim) };
}

} // namespace

TEST_CASE("measured: gases diffuse in air at NIST's and Incropera's coefficients (Stage 50)") {
	const SpeciesTable &t = builtin_species();
	// at 298.15 K and 1 atm: water in nitrogen 0.254 cm^2/s (NIST TN 2279), carbon dioxide and oxygen in air 0.160 and
	// 0.21 (Incropera's Table A.8), nitrogen as oxygen's partner in air 0.21
	CHECK(gas_diffusion(t, builtin::steam, 298.15) == doctest::Approx(0.254e-4).epsilon(1e-12));
	CHECK(gas_diffusion(t, builtin::carbon_dioxide, 298.15) == doctest::Approx(0.160e-4).epsilon(1e-12));
	CHECK(gas_diffusion(t, builtin::oxygen, 298.15) == doctest::Approx(0.21e-4).epsilon(1e-12));
	CHECK(gas_diffusion(t, builtin::nitrogen, 298.15) == doctest::Approx(0.21e-4).epsilon(1e-12));
	CHECK(gas_diffusion(t, builtin::water, 298.15) == 0.0); // a liquid
	CHECK(gas_diffusion(t, builtin::glass, 298.15) == 0.0);
	// water's power of the temperature holds NIST's own fit, ln D = -10.324 - 84.181 / T + 1.620 ln T, within 1 %
	// from 280 to 450 K
	for (double kelvin = 280.0; kelvin <= 450.0; kelvin += 10.0) {
		const double nist = std::exp(-10.324 - 84.181 / kelvin + 1.620 * std::log(kelvin)) * 1e-4;
		CAPTURE(kelvin);
		CHECK(gas_diffusion(t, builtin::steam, kelvin) == doctest::Approx(nist).epsilon(0.01));
	}
}

TEST_CASE("exchange: a flask's carbon dioxide leaves through its mouth at the two cells' relaxation (Stage 50)") {
	Open o = open_flask(0.004, 0.0);
	const World &w = o.sim.world();
	const Totals genesis = totals(builtin_species(), w.snapshot());
	const double x0 = mol_of(w, o.flask, builtin::carbon_dioxide) / gas_mol(w, o.flask);
	// the fraction's difference decays as exp(-D c G (1/Na + 1/Nb) t)
	const double d = gas_diffusion(builtin_species(), builtin::carbon_dioxide, 293.15), c = P_ATMOSPHERE / (GAS_CONSTANT * 293.15);
	const double rate = d * c * MOUTH * (1.0 / gas_mol(w, o.flask) + 1.0 / gas_mol(w, o.air));
	const double seconds = 120.0;
	for (int k = 0; k < static_cast<int>(seconds * 60.0); ++k) {
		o.sim.step();
	}
	const double xf = mol_of(w, o.flask, builtin::carbon_dioxide) / gas_mol(w, o.flask), xa = mol_of(w, o.air, builtin::carbon_dioxide) / gas_mol(w, o.air);
	MESSAGE("CO2 in the flask: " << x0 << " -> " << xf << ", time constant " << 1.0 / rate << " s");
	CHECK((xf - xa) / x0 == doctest::Approx(std::exp(-rate * seconds)).epsilon(0.02));
	CHECK(xf < x0 * 0.95); // it went
	CHECK(totals(builtin_species(), w.snapshot()) == genesis);
}

TEST_CASE("exchange: an open flask of water evaporates into dry air at Stefan's rate (Stage 50)") {
	Open o = open_flask(0.0, 2.0);
	const World &w = o.sim.world();
	for (int k = 0; k < 60; ++k) {
		o.sim.step(); // its gas saturates
	}
	const double start = mol_of(w, o.flask, builtin::water) + mol_of(w, o.flask, builtin::steam);
	const double x_sat = mol_of(w, o.flask, builtin::steam) / gas_mol(w, o.flask);
	const double seconds = 600.0;
	double room = 0.0; // the room's own steam, as it gathers, averaged
	for (int k = 0; k < static_cast<int>(seconds * 60.0); ++k) {
		o.sim.step();
		room += mol_of(w, o.air, builtin::steam) / gas_mol(w, o.air) / (seconds * 60.0);
	}
	const double lost = start - (mol_of(w, o.flask, builtin::water) + mol_of(w, o.flask, builtin::steam));
	// Stefan's tube, the gas dilute: D c G (x_sat - x_room) mol/s
	const double expected = gas_diffusion(builtin_species(), builtin::steam, 293.15) * P_ATMOSPHERE / (GAS_CONSTANT * 293.15) * MOUTH * (x_sat - room) * seconds;
	MESSAGE("saturated at " << x_sat * P_ATMOSPHERE / 1000.0 << " kPa; lost " << lost * 18.015 * 1000.0 << " mg in " << seconds << " s");
	CHECK(x_sat * P_ATMOSPHERE == doctest::Approx(2339.0).epsilon(0.04)); // water's vapour pressure at 20 C, 2.339 kPa (the tables' within their 2.5 %)
	CHECK(lost == doctest::Approx(expected).epsilon(0.02));
}

TEST_CASE("invariants: E1 fires on a gas moved against its fraction, past equal, or along an edge that does not exchange (Stage 50)") {
	Open o = open_flask(0.004, 0.0);
	const StepTrace good = o.sim.step();
	REQUIRE(!good.exchanged.empty());
	CHECK(check_E1(builtin_species(), good).empty());
	StepTrace against = good;
	std::swap(against.exchanged[0].x_from, against.exchanged[0].x_to);
	CHECK(!check_E1(builtin_species(), against).empty());
	CHECK(check_E1(builtin_species(), against)[0].rfind("E1", 0) == 0);
	StepTrace past = good;
	past.exchanged[0].moved = static_cast<int64_t>(1e15);
	CHECK(!check_E1(builtin_species(), past).empty());
	StepTrace elsewhere = good;
	elsewhere.edges[static_cast<size_t>(elsewhere.exchanged[0].edge.index)].exchange = 0.0;
	CHECK(!check_E1(builtin_species(), elsewhere).empty());
	// refused: a negative or a non-finite exchange; mid-step
	World::Builder b(builtin_species());
	const CellId a = b.add_cell("a"), c = b.add_cell("c");
	const EdgeId e = b.add_edge(a, c, PHASES_GAS);
	auto world = b.build();
	REQUIRE(world.has_value());
	CHECK(world->set_exchange(e, -1.0) == Refusal::BadValue);
	CHECK(world->set_exchange(e, std::nan("")) == Refusal::BadValue);
	CHECK(world->set_exchange(EdgeId{ 9 }, 1.0) == Refusal::UnknownEdge);
}
