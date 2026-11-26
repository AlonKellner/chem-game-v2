// Liquids that expand (GAME_SPEC §6, Stage 67): a liquid's and a solute's volume grow with the solvent's density from
// 25 C (Kell's), solids keep theirs. Open, nothing else changes (its expansion against the room is in Cp); sealed, its
// solids and liquids have the Gibbs energy G0(T) + V_c(T) (P - P_atm), so its energy, its entropy and its species'
// potentials are one potential's, its temperature E's root, and its gas squeezed as its liquid grows.
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

double water_molar_volume() {
	return builtin_species().species(builtin::water).molar_volume;
}

// a sealed flask of `ml` of water (at 25 C) and air at 1 atm in what it leaves at `kelvin`
World sealed_flask(double ml, double kelvin, double burner_kelvin = 0.0) {
	World::Builder b(builtin_species());
	const CellId flask = b.add_rigid_cell("flask", FLASK);
	const double water = ml * 1e-6 / water_molar_volume();
	const double air = P_ATMOSPHERE * (FLASK - ml * 1e-6 * expansion(builtin_species(), kelvin)) / (GAS_CONSTANT * kelvin);
	b.set_amount(flask, builtin::water, mol(water)).set_amount(flask, builtin::nitrogen, mol(0.79 * air)).set_amount(flask, builtin::oxygen, mol(0.21 * air));
	b.set_temperature(flask, kelvin);
	if (burner_kelvin > 0.0) {
		const CellId burner = b.add_reservoir("burner", burner_kelvin);
		b.add_edge(flask, burner, PHASES_NONE, 50.0);
	}
	std::string why;
	auto built = b.build(&why);
	REQUIRE_MESSAGE(built.has_value(), why);
	REQUIRE(built->meta(flask).sealed);
	return std::move(*built);
}

// its state with its energy moved by `joule`
CellState with_energy(const CellState &cell, double joule) {
	CellState out = cell;
	bool ok = false;
	out.energy += energy_from_joule(joule, ok).quanta;
	REQUIRE(ok);
	return out;
}

} // namespace

TEST_CASE("expansion: water's volume grows as Kell's density falls from 25 C, its solutes' with it, a solid's not at all (Stage 67)") {
	const SpeciesTable &table = builtin_species();
	CHECK(expansion(table, T0) == doctest::Approx(1.0).epsilon(1e-15));
	CHECK(expansion(table, 373.15) == doctest::Approx(997.0449 / 958.3637).epsilon(1e-6)); // about 4.04%
	CHECK(expansion(table, 293.15) == doctest::Approx(997.0449 / 998.2041).epsilon(1e-6));
	CHECK(expansion(table, 277.13) < expansion(table, 273.15)); // the density's maximum near 4 C
	CHECK(expansion(table, 277.13) < expansion(table, 281.15));
	// its slope, against its own differences, and continuous past its range's end (150 C)
	for (const double t : { 280.0, 300.0, 350.0, 400.0, 450.0 }) { // (its curve has a step at the range's end, 150 C)
		double slope = 0.0, curve = 0.0;
		expansion(table, t, &slope, &curve);
		const double h = 1e-3;
		CHECK(slope == doctest::Approx((expansion(table, t + h) - expansion(table, t - h)) / (2.0 * h)).epsilon(1e-6));
		double up = 0.0, down = 0.0;
		expansion(table, t + h, &up);
		expansion(table, t - h, &down);
		CHECK(curve == doctest::Approx((up - down) / (2.0 * h)).epsilon(1e-4).scale(1e-7));
	}
	const SpeciesDef &oxide = table.species(builtin::copper_oxide), &solute = table.species(builtin::copper_sulfate_aq), &water = table.species(builtin::water);
	CHECK(molar_volume_at(table, oxide, 373.15) == oxide.molar_volume);
	CHECK(molar_volume_at(table, water, 373.15) == doctest::Approx(water.molar_volume * expansion(table, 373.15)).epsilon(1e-15));
	CHECK(molar_volume_at(table, solute, 373.15) == doctest::Approx(solute.molar_volume * expansion(table, 373.15)).epsilon(1e-15));
	// a cell's fill: its water's volume at its temperature, its oxide's fixed
	CellState cell;
	cell.amount.assign(table.species_count(), 0);
	cell.particles.assign(table.species_count(), 0);
	cell.amount[static_cast<size_t>(builtin::water.index)] = mol(10.0).quanta;
	cell.amount[static_cast<size_t>(builtin::copper_oxide.index)] = mol(1.0).quanta;
	const Fill cold = fill_of(table, cell, T0), hot = fill_of(table, cell, 363.15);
	CHECK(hot.condensed - cold.condensed == doctest::Approx(10.0 * water.molar_volume * (expansion(table, 363.15) - 1.0)).epsilon(1e-9));
}

TEST_CASE("expansion: a sealed cell's temperature is its energy's root: set at a temperature it reads back, and its heat capacity is dE/dT (Stage 67)") {
	for (const double ml : { 50.0, 200.0, 240.0 }) {
		for (const double kelvin : { 280.0, 298.15, 330.0, 360.0 }) {
			const World world = sealed_flask(ml, kelvin);
			const CellId flask{ 0 };
			const Thermal t = thermal_state(world.species(), world.snapshot_cell(flask), &world.meta(flask));
			REQUIRE(t.defined);
			CHECK(t.temperature == doctest::Approx(kelvin).epsilon(1e-12));
			CHECK(t.pressure == doctest::Approx(P_ATMOSPHERE).epsilon(1e-6)); // filled with air at 1 atm in the room its water leaves at T
			const double dj = 1.0;
			const Thermal up = thermal_state(world.species(), with_energy(world.snapshot_cell(flask), dj), &world.meta(flask));
			const Thermal down = thermal_state(world.species(), with_energy(world.snapshot_cell(flask), -dj), &world.meta(flask));
			REQUIRE(up.defined);
			REQUIRE(down.defined);
			CHECK(2.0 * dj / (up.temperature - down.temperature) == doctest::Approx(t.heat_capacity).epsilon(1e-6));
		}
	}
}

TEST_CASE("expansion: a sealed cell's energy and entropy are one potential's: dS = dE / T as it is heated, at any pressure (T2) (Stage 67)") {
	const SpeciesTable &table = builtin_species();
	for (const double ml : { 100.0, 230.0 }) {
		const World world = sealed_flask(ml, 300.0);
		const CellId flask{ 0 };
		const CellState base = world.snapshot_cell(flask);
		for (const double heat : { 0.0, 2e4, 6e4 }) { // J: warmed, its gas squeezed by its water and its own heat
			const CellState at = with_energy(base, heat);
			const Thermal t = thermal_state(table, at, &world.meta(flask));
			REQUIRE(t.defined);
			const double dj = 10.0;
			const CellState up = with_energy(at, dj), down = with_energy(at, -dj);
			const Thermal tu = thermal_state(table, up, &world.meta(flask)), td = thermal_state(table, down, &world.meta(flask));
			const double ds = entropy(table, up, tu.temperature, &world.meta(flask)) - entropy(table, down, td.temperature, &world.meta(flask));
			MESSAGE(ml << " mL at " << t.temperature - 273.15 << " C: " << t.pressure / 1000.0 << " kPa");
			CHECK(ds * t.temperature / (2.0 * dj) == doctest::Approx(1.0).epsilon(1e-7));
		}
	}
}

TEST_CASE("expansion: a sealed cell's affinity is its entropy's slope along the reaction at fixed energy, its liquid's potential at its pressure (Stage 67)") {
	const SpeciesTable &table = builtin_species();
	const World world = sealed_flask(230.0, 300.0);
	const CellId flask{ 0 };
	CellState hot = with_energy(world.snapshot_cell(flask), 5e4);
	hot.amount[static_cast<size_t>(builtin::steam.index)] += mol(1e-4).quanta; // some vapour: its affinity finite
	const Thermal t = thermal_state(table, hot, &world.meta(flask));
	REQUIRE(t.defined);
	REQUIRE(t.pressure > 1.2 * P_ATMOSPHERE);
	const Reaction *boil = nullptr;
	for (const Reaction &r : builtin_reactions()) {
		if (r.nu(builtin::water) == -1 && r.nu(builtin::steam) == 1) {
			boil = &r;
		}
	}
	REQUIRE(boil != nullptr);
	const AffinityProbe probe(table, hot, boil->terms(), &world.meta(flask));
	REQUIRE(probe.defined());
	double a0 = 0.0;
	REQUIRE(probe.at(0.0, a0));
	bool defined = false;
	CHECK(affinity(table, hot, boil->terms(), t.temperature, defined, &world.meta(flask)) == doctest::Approx(a0).epsilon(1e-12));
	// T dS/dx at fixed E: x quanta of water to steam, the energy unchanged
	const double x = 1e6; // quanta
	CellState fwd = hot, back = hot;
	fwd.amount[static_cast<size_t>(builtin::water.index)] -= static_cast<int64_t>(x);
	fwd.amount[static_cast<size_t>(builtin::steam.index)] += static_cast<int64_t>(x);
	back.amount[static_cast<size_t>(builtin::water.index)] += static_cast<int64_t>(x);
	back.amount[static_cast<size_t>(builtin::steam.index)] -= static_cast<int64_t>(x);
	const Thermal tf = thermal_state(table, fwd, &world.meta(flask)), tb = thermal_state(table, back, &world.meta(flask));
	REQUIRE(tf.defined);
	REQUIRE(tb.defined);
	const double ds = entropy(table, fwd, tf.temperature, &world.meta(flask)) - entropy(table, back, tb.temperature, &world.meta(flask));
	const double slope = t.temperature * ds / (2.0 * x * MOL_PER_QUANTUM); // J/mol
	// (to the difference's own precision, 2e-5: the potential's term at its pressure, v (P - P_atm), is 1e-3 of it)
	CHECK(slope == doctest::Approx(a0).epsilon(1e-4).scale(1.0));
	// the probe's temperature along it is the state's
	double af = 0.0;
	REQUIRE(probe.at(x, af));
	CHECK(af == doctest::Approx(affinity(table, fwd, boil->terms(), tf.temperature, defined, &world.meta(flask))).epsilon(1e-9).scale(1.0));
}

TEST_CASE("expansion: a sealed flask nearly full of water heated: its pressure past the gas law's at its own volume, as its water squeezes its air, every invariant held (Stage 67)") {
	World world = sealed_flask(230.0, 293.15, 380.0); // (a step is 1/60 s: a burner of 50 W/K)
	const CellId flask{ 0 };
	const double air0 = FLASK - 230e-6 * expansion(builtin_species(), 293.15);
	Simulation sim(std::move(world), equilibria());
	sim.set_checking(false);
	for (int i = 0; i < 60 * 30; ++i) {
		const StepTrace trace = sim.step();
		const Violations v = check_all(sim.world().species(), trace, sim.equilibria());
		REQUIRE_MESSAGE(v.empty(), "step " << i << ": " << v[0]);
	}
	const Thermal t = thermal_state(sim.world().species(), sim.world().snapshot_cell(flask), &sim.world().meta(flask));
	REQUIRE(t.defined);
	const double gas_law = P_ATMOSPHERE * t.temperature / 293.15; // its air alone, at its first volume
	MESSAGE("230 mL of water in a 250 mL sealed flask, at " << t.temperature - 273.15 << " C: " << t.pressure / 1000.0 << " kPa (the air alone, unsqueezed: "
										<< gas_law / 1000.0 << " kPa); its free volume " << air0 * 1e6 << " -> " << t.free_volume * 1e6 << " mL");
	CHECK(t.temperature > 340.0);
	CHECK(t.free_volume < air0 * 0.9);
	CHECK(t.pressure > gas_law * 1.1);
}

TEST_CASE("expansion: an open vessel filled to its brim and heated spills what its water grows by into the spill, every invariant held (Stage 67)") {
	World::Builder b(builtin_species());
	const double volume = 100e-6;
	const CellId beaker = b.add_rigid_cell("beaker", volume);
	const CellId room = b.add_cell("room");
	const CellId burner = b.add_reservoir("burner", 360.0);
	const double water = volume / water_molar_volume() * expansion(builtin_species(), T0) / expansion(builtin_species(), 293.15); // to the brim at 20 C
	b.set_amount(beaker, builtin::water, mol(water * (1.0 - 1e-9))).set_temperature(beaker, 293.15);
	const double air = 10.0;
	b.set_amount(room, builtin::nitrogen, mol(0.79 * air)).set_amount(room, builtin::oxygen, mol(0.21 * air)).set_temperature(room, 293.15);
	b.add_edge(beaker, room, PHASES_GAS);
	b.add_edge(beaker, burner, PHASES_NONE, 30.0);
	b.set_spill(room);
	std::string why;
	auto built = b.build(&why);
	REQUIRE_MESSAGE(built.has_value(), why);
	Simulation sim(std::move(*built), equilibria());
	sim.set_checking(false);
	for (int i = 0; i < 60 * 10; ++i) {
		const StepTrace trace = sim.step();
		const Violations v = check_all(sim.world().species(), trace, sim.equilibria());
		REQUIRE_MESSAGE(v.empty(), "step " << i << ": " << v[0]);
	}
	const Thermal t = thermal_state(sim.world().species(), sim.world().snapshot_cell(beaker));
	REQUIRE(t.defined);
	const double left = static_cast<double>(sim.world().snapshot_cell(beaker).amount[static_cast<size_t>(builtin::water.index)]) * MOL_PER_QUANTUM;
	// what ran into the room, as water or (into its air) as steam
	const CellState &spill = sim.world().snapshot_cell(room);
	const double spilled = static_cast<double>(spill.amount[static_cast<size_t>(builtin::water.index)] + spill.amount[static_cast<size_t>(builtin::steam.index)]) * MOL_PER_QUANTUM;
	const double steamed = static_cast<double>(sim.world().snapshot_cell(beaker).amount[static_cast<size_t>(builtin::steam.index)]) * MOL_PER_QUANTUM;
	MESSAGE("a brim-full beaker heated to " << t.temperature - 273.15 << " C spilled " << spilled * water_molar_volume() * 1e6 << " mL (at 25 C)");
	CHECK(t.temperature > 320.0);
	CHECK(left * molar_volume_at(sim.world().species(), sim.world().species().species(builtin::water), t.temperature) == doctest::Approx(volume).epsilon(1e-6));
	// what spilled is about what its water grew by (some went up as steam from its surface instead)
	CHECK(spilled == doctest::Approx(water - left - steamed).epsilon(1e-6));
	CHECK(spilled > 0.5 * water * (1.0 - expansion(sim.world().species(), 293.15) / expansion(sim.world().species(), t.temperature)));
}
