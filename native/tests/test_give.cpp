// Liquids that give (GAME_SPEC §6, Stage 88): a sealed cell's liquids are compressible (the solvent's, Kell's), their
// Gibbs energy G0(T) + V_s p + V_l(T) (p - k p^2 / 2), p = P - P_atm. Every state then has one pressure: gas over
// liquid, liquid alone (P_atm - A / B), or nought with a void left; and its energy, its entropy and its species'
// potentials are that one potential's.
#include "chem/builtin_ids.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"
#include "tables.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

using namespace chem;

namespace {

constexpr double ML = 1e-6; // m^3

Amount mol(double n) {
	bool ok = false;
	const Amount a = amount_from_mol(n, ok);
	REQUIRE(ok);
	return a;
}

std::vector<Reaction> equilibria() {
	std::vector<Reaction> out;
	for (const Reaction &r : builtin_reactions()) {
		if (r.kinetics().law == Kinetics::Law::Equilibrium) {
			out.push_back(r);
		}
	}
	return out;
}

// what `ml` of water at `kelvin` is, mol
double water_mol(double ml, double kelvin) {
	return ml * ML / molar_volume_at(builtin_species(), builtin_species().species(builtin::water), kelvin);
}

// a sealed vessel of `volume_ml` holding `water` mol of water and `air` mol of air at `kelvin`; a `conductance` (W/K)
// to a reservoir at `outside` K
World vessel(double volume_ml, double water, double air, double kelvin, double conductance = 0.0, double outside = 0.0) {
	World::Builder b(builtin_species());
	const CellId cell = b.add_rigid_cell("vessel", volume_ml * ML);
	if (water > 0.0) {
		b.set_amount(cell, builtin::water, mol(water));
	}
	if (air > 0.0) {
		b.set_amount(cell, builtin::nitrogen, mol(0.79 * air)).set_amount(cell, builtin::oxygen, mol(0.21 * air));
	}
	b.set_temperature(cell, kelvin);
	if (conductance > 0.0) {
		b.add_edge(cell, b.add_reservoir("outside", outside), PHASES_NONE, conductance);
	}
	std::string why;
	auto built = b.build(&why);
	REQUIRE_MESSAGE(built.has_value(), why);
	REQUIRE(built->meta(cell).sealed);
	return std::move(*built);
}

Thermal state(const World &world, double volume_ml = 0.0) {
	CellMeta meta = world.meta(CellId{ 0 });
	if (volume_ml > 0.0) {
		meta.volume = volume_ml * ML;
	}
	return thermal_state(world.species(), world.snapshot_cell(CellId{ 0 }), &meta);
}

} // namespace

TEST_CASE("give: a sealed vessel full of water, no gas, has a pressure: the room's where its water just fits, P_atm - A / B squeezed or let out a little (Stage 88)") {
	const SpeciesTable &table = builtin_species();
	const double kelvin = 293.15, n = water_mol(100.0, kelvin), k = compressibility(table, kelvin);
	const World world = vessel(100.0, n, 0.0, kelvin);
	// (its volume its water's at 20 C to a quantum of it: the room's pressure, within what a quantum's volume gives)
	const Thermal fits = state(world);
	REQUIRE(fits.defined);
	CHECK(fits.temperature == doctest::Approx(kelvin).epsilon(1e-9));
	CHECK(fits.pressure == doctest::Approx(P_ATMOSPHERE).epsilon(1e-6));
	CHECK(fits.free_volume == 0.0);
	// a millionth less room, its energy as it was (no heat: its entropy stands, to the second order): 1e-6 over its
	// compressibility at its entropy, which is its isothermal one over Cp / Cv = 1 / (1 - T alpha^2 V / (k Cp)), water's
	// 1.0066 at 20 C: the model's own, from its potential
	double grows = 0.0;
	const double e = expansion(table, kelvin, &grows), alpha = grows / e;
	const double ratio = 1.0 / (1.0 - kelvin * alpha * alpha * 100.0 * ML / (k * fits.heat_capacity));
	CHECK(ratio == doctest::Approx(1.0066).epsilon(2e-4));
	const Thermal squeezed = state(world, 100.0 * (1.0 - 1e-6));
	REQUIRE(squeezed.defined);
	CHECK(squeezed.pressure - P_ATMOSPHERE == doctest::Approx(1e-6 / k * ratio).epsilon(1e-3));
	MESSAGE("a millionth squeezed: " << (squeezed.pressure - P_ATMOSPHERE) / 1e5 << " bar over the room's; Cp / Cv " << ratio);
	// a hand's 74 kPa on it, held long enough to stay at the room's temperature: 100 mL gives 0.0034 mL
	const double hand = 74e3, gives = 100.0 * k * hand;
	CHECK(gives == doctest::Approx(0.0034).epsilon(0.02));
	CHECK(state(world, 100.0 - gives).pressure - P_ATMOSPHERE == doctest::Approx(hand * ratio).epsilon(1e-3));
	// a little more room than it takes at no pressure, and no gas: a void, at no pressure
	const Thermal pulled = state(world, 100.0 * (1.0 + 2.0 * k * P_ATMOSPHERE));
	REQUIRE(pulled.defined);
	CHECK(pulled.pressure == 0.0);
	CHECK(pulled.free_volume == doctest::Approx(100.0 * ML * k * P_ATMOSPHERE).epsilon(1e-3));
}

TEST_CASE("give: brim-full water warmed in a sealed vessel rises by its expansion over its compressibility, 4.5 bar a kelvin at 20 C and 10 at 50 C, every invariant held (Stage 88)") {
	const SpeciesTable &table = builtin_species();
	for (const double celsius : { 20.0, 50.0 }) {
		const double kelvin = celsius + 273.15;
		double grows = 0.0;
		const double e = expansion(table, kelvin, &grows), k = compressibility(table, kelvin);
		const double by_kelvin = grows / e / k; // alpha / kappa, Pa/K
		// warmed by a bath a kelvin over it
		World world = vessel(100.0, water_mol(100.0, kelvin), 0.0, kelvin, 50.0, kelvin + 1.0);
		Simulation sim(std::move(world), equilibria());
		sim.set_checking(false);
		const double before = state(sim.world()).pressure;
		for (int i = 0; i < 60 * 120; ++i) {
			const StepTrace trace = sim.step();
			const Violations v = check_all(table, trace, sim.equilibria());
			REQUIRE_MESSAGE(v.empty(), "step " << i << ": " << v[0]);
		}
		const Thermal after = state(sim.world());
		REQUIRE(after.defined);
		CHECK(after.temperature == doctest::Approx(kelvin + 1.0).epsilon(1e-6));
		MESSAGE(celsius << " C: " << (after.pressure - before) / 1e5 << " bar for a kelvin (alpha / kappa: " << by_kelvin / 1e5 << ")");
		// exactly: P_atm + (1 - V / V_l(T)) / k(T) at the temperature it came to, its volume its water's at the start
		const double closed = P_ATMOSPHERE + (1.0 - e / expansion(table, after.temperature)) / compressibility(table, after.temperature);
		CHECK(after.pressure == doctest::Approx(closed).epsilon(1e-5));
		// (a kelvin's rise is not quite its slope at the start: its expansion grows 5 % a kelvin at 20 C)
		CHECK(after.pressure - before == doctest::Approx(by_kelvin).epsilon(0.04));
		CHECK(by_kelvin / 1e5 == doctest::Approx(celsius == 20.0 ? 4.5 : 10.4).epsilon(0.02));
	}
}

TEST_CASE("give: a sealed cell's pressure falls as its volume grows, without a break, from water squeezed alone through a pocket of air to air over water (Stage 88)") {
	// 100 mL of water and the air of 0.1 mL at 1 atm: from 0.2 % too small for the water to twice the pocket
	const double kelvin = 298.15, air = P_ATMOSPHERE * 0.1 * ML / (GAS_CONSTANT * kelvin);
	const World world = vessel(100.1, water_mol(100.0, kelvin), air, kelvin);
	CHECK(state(world).pressure == doctest::Approx(P_ATMOSPHERE).epsilon(1e-4));
	double last = 0.0;
	for (int i = 0; i <= 400; ++i) {
		const double ml = 99.8 + 0.001 * i;
		const Thermal t = state(world, ml);
		CAPTURE(ml);
		REQUIRE(t.defined);
		REQUIRE(t.pressure > 0.0);
		REQUIRE(t.free_volume > 0.0); // (its air has room, however squeezed)
		if (i > 0) {
			REQUIRE(t.pressure < last);
		}
		last = t.pressure;
	}
	// squeezed to the water's own volume the pocket holds what squeezes the water with it, not no pressure: the root of
	// P (A + B (P - P_atm)) = n R T at the temperature it comes to, A what the water leaves at the room's pressure there
	// and B its volume times its compressibility: 5.3 bar, the air in 19 uL
	const Thermal tight = state(world, 100.0);
	MESSAGE("at the water's own 100 mL: " << tight.pressure / 1e5 << " bar, its air in " << tight.free_volume / ML * 1000.0 << " uL");
	const double liquid = 100.0 * ML * expansion(builtin_species(), tight.temperature), a = 100.0 * ML - liquid;
	const double b = liquid * compressibility(builtin_species(), tight.temperature), nrt = air * GAS_CONSTANT * tight.temperature;
	CHECK(tight.pressure * (a + b * (tight.pressure - P_ATMOSPHERE)) == doctest::Approx(nrt).epsilon(1e-6));
	CHECK(tight.pressure * tight.free_volume == doctest::Approx(nrt).epsilon(1e-6));
	CHECK(tight.pressure == doctest::Approx(5.3e5).epsilon(0.01));
}

TEST_CASE("give: a sealed cell's energy, entropy and pressure are one potential's at any pressure: dE = T dS at its volume, dE = -(P - P_atm) dV at its entropy, its heat capacity its energy's slope (T2) (Stage 88)") {
	const SpeciesTable &table = builtin_species();
	struct Case {
		const char *name;
		double volume_ml, air_ml;
	};
	// water alone squeezed hard and lightly, a pocket of air, air over water
	for (const Case &c : { Case{ "water at 33 bar", 99.86, 0.0 }, Case{ "water at 300 bar", 98.7, 0.0 }, Case{ "water at 2 bar", 99.995, 0.0 }, Case{ "a pocket", 100.02, 0.1 },
				 Case{ "air over water", 150.0, 50.0 }, Case{ "air over water, squeezed", 120.0, 50.0 } }) {
		const double kelvin = 310.0;
		const World world = vessel(100.0 + c.air_ml, water_mol(100.0, kelvin), P_ATMOSPHERE * c.air_ml * ML / (GAS_CONSTANT * kelvin), kelvin);
		CellMeta meta = world.meta(CellId{ 0 });
		meta.volume = c.volume_ml * ML;
		const CellState base = world.snapshot_cell(CellId{ 0 });
		const Thermal t = thermal_state(table, base, &meta);
		REQUIRE(t.defined);
		MESSAGE(std::string(c.name) << ": " << t.pressure / 1e5 << " bar at " << t.temperature - 273.15 << " C");
		const auto with_energy = [&](double joule) {
			CellState out = base;
			bool ok = false;
			out.energy += energy_from_joule(joule, ok).quanta;
			REQUIRE(ok);
			return out;
		};
		const auto entropy_of = [&](const CellState &x, const CellMeta &m) {
			const Thermal at = thermal_state(table, x, &m);
			REQUIRE(at.defined);
			return entropy(table, x, at.temperature, &m);
		};
		// at its volume: dS = dE / T, and its heat capacity is dE / dT
		const double dj = 5.0;
		const CellState up = with_energy(dj), down = with_energy(-dj);
		CHECK((entropy_of(up, meta) - entropy_of(down, meta)) * t.temperature / (2.0 * dj) == doctest::Approx(1.0).epsilon(1e-6));
		const double dt = thermal_state(table, up, &meta).temperature - thermal_state(table, down, &meta).temperature;
		CHECK(2.0 * dj / dt == doctest::Approx(t.heat_capacity).epsilon(1e-5));
		// its volume moved a little either way, its energy by the work on it at its pressure: its entropy stands (the two
		// sides' second orders cancel)
		const double dv = meta.volume * 1e-6;
		CellMeta wider = meta, tighter = meta;
		wider.volume += dv;
		tighter.volume -= dv;
		const double work = (t.pressure - P_ATMOSPHERE) * dv;
		const double ds = entropy_of(with_energy(-work), wider) - entropy_of(with_energy(work), tighter);
		// (against what the work alone would be as heat: 2 work / T)
		CHECK(std::fabs(ds) * t.temperature < 1e-4 * std::max(2.0 * std::fabs(work), 1e-4));
	}
}

TEST_CASE("give: water sealed with more room than it takes and no gas stands at no pressure, and boils into the room: a cavity at its vapour pressure (Stage 88)") {
	const SpeciesTable &table = builtin_species();
	const double kelvin = 298.15;
	World world = vessel(110.0, water_mol(100.0, kelvin), 0.0, kelvin, 5.0, kelvin);
	const Thermal empty = state(world);
	REQUIRE(empty.defined);
	CHECK(empty.pressure == 0.0);
	CHECK(empty.free_volume == doctest::Approx(10.0 * ML).epsilon(1e-3));
	Simulation sim(std::move(world), equilibria());
	sim.set_checking(false);
	for (int i = 0; i < 60 * 60; ++i) {
		const StepTrace trace = sim.step();
		const Violations v = check_all(table, trace, sim.equilibria());
		REQUIRE_MESSAGE(v.empty(), "step " << i << ": " << v[0]);
	}
	const Thermal after = state(sim.world());
	REQUIRE(after.defined);
	MESSAGE("the cavity: " << after.pressure / 1000.0 << " kPa at " << after.temperature - 273.15 << " C");
	CHECK(sim.world().snapshot_cell(CellId{ 0 }).amount[static_cast<size_t>(builtin::steam.index)] > 0);
	CHECK(after.pressure == doctest::Approx(3170.0).epsilon(0.03)); // water's vapour pressure at 25 C (3.17 kPa)
}

TEST_CASE("give: what crosses carries its enthalpy at the giver's state: water at a pressure takes v p less what its expansion holds back, the slope of its cell's enthalpy by its water at that temperature and pressure (Stage 88)") {
	const SpeciesTable &table = builtin_species();
	const SpeciesDef &water = table.species(builtin::water);
	// a mole of water at a bar over the room's: v p at 4 C, where it does not expand; 6 % less at 20 C, 14 % at 50 C
	const SpeciesAmount one[] = { SpeciesAmount{ builtin::water, mol(1.0) } };
	const double p = 1e5;
	for (const double celsius : { 3.98, 20.0, 50.0 }) {
		const double kelvin = celsius + 273.15;
		double grows = 0.0, dk = 0.0;
		const double e = expansion(table, kelvin, &grows), k = compressibility(table, kelvin, &dk), v = water.molar_volume * e;
		const double over = enthalpy_over(table, one, kelvin, p);
		CAPTURE(celsius);
		CHECK(over == doctest::Approx(v * (p - 0.5 * k * p * p) - kelvin * (water.molar_volume * grows * (p - 0.5 * k * p * p) - v * dk * 0.5 * p * p)).epsilon(1e-12));
		CHECK(over / (v * p) == doctest::Approx(celsius < 4.0 ? 1.0 : celsius < 21.0 ? 0.939 : 0.852).epsilon(2e-3));
	}
	CHECK(enthalpy_over(table, one, 293.15, 0.0) == 0.0);
	const SpeciesAmount gas[] = { SpeciesAmount{ builtin::nitrogen, mol(1.0) } };
	CHECK(enthalpy_over(table, gas, 293.15, p) == 0.0);
	// against the sealed model itself: a cell's enthalpy is H = E + p V (E = U + P_atm V), and what a mole of water
	// carries at a temperature and a pressure is H's slope by its water there, (dH/dn) at T and P: the table's and
	// what the pressure adds. Water under air at 3 and at 30 bar, and water alone at 30 bar
	struct Case {
		double air_ml, bar;
	};
	for (const Case &c : { Case{ 50.0, 3.0 }, Case{ 50.0, 30.0 }, Case{ 0.0, 30.0 } }) {
		const double kelvin = 293.15, target = c.bar * 1e5, air = P_ATMOSPHERE * c.air_ml * ML / (GAS_CONSTANT * kelvin);
		// the cell's enthalpy with `water` mol of water at that temperature and pressure: its volume the one that holds
		// the pressure there
		const auto enthalpy_with = [&](double water) {
			const World world = vessel(101.0 + c.air_ml, water, air, kelvin); // (built with room for a little more water)
			const CellState cell = world.snapshot_cell(CellId{ 0 });
			CellMeta meta = world.meta(CellId{ 0 });
			double lo = 90.0 * ML, hi = (101.0 + c.air_ml) * ML, joule = 0.0;
			for (int i = 0; i < 200; ++i) {
				meta.volume = 0.5 * (lo + hi);
				CellState at = cell;
				bool ok = false;
				REQUIRE(energy_at(table, cell, &meta, kelvin, joule));
				at.energy = energy_from_joule(joule, ok).quanta;
				const Thermal t = thermal_state(table, at, &meta);
				REQUIRE(t.defined);
				(t.pressure > target ? lo : hi) = meta.volume; // (its pressure falls as its volume grows)
			}
			return joule + (target - P_ATMOSPHERE) * meta.volume;
		};
		const double n = water_mol(100.0, kelvin), dn = 1e-3 * n;
		const double slope = (enthalpy_with(n + dn) - enthalpy_with(n - dn)) / (2.0 * dn);
		const double carried = water.hf + water.cp * (kelvin - T0) + enthalpy_over(table, one, kelvin, target - P_ATMOSPHERE);
		const double table_alone = water.hf + water.cp * (kelvin - T0);
		MESSAGE(c.bar << " bar" << std::string(c.air_ml > 0.0 ? ", under air" : ", water alone") << ": dH/dn less the table's " << slope - table_alone << " J/mol; the pressure adds "
					  << carried - table_alone);
		CHECK(slope - table_alone == doctest::Approx(carried - table_alone).epsilon(2e-3));
	}
}
