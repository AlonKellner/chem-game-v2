// Pressure (GAME_SPEC §6, Stage 28): a rigid cell with no gas path to an elastic
// one is sealed; its energy is E = U + P_atm V, its pressure n_gas R T / V_free,
// and its fast equilibria are taken at that pressure, so water boils where the
// steam tables say it boils at that pressure. Cells sealed together share one
// pressure; one opened again is at the room's within the step.
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

Thermal state(const World &world, CellId cell) {
	const Thermal t = thermal_state(world.species(), world.snapshot_cell(cell), &world.meta(cell));
	REQUIRE(t.defined);
	return t;
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

void run(Simulation &sim, int steps) {
	sim.set_checking(false);
	for (int i = 0; i < steps; ++i) {
		const StepTrace trace = sim.step();
		const Violations v = check_all(sim.world().species(), trace, sim.equilibria());
		REQUIRE_MESSAGE(v.empty(), "step " << i << ": " << v[0]);
	}
}

} // namespace

TEST_CASE("pressure: a sealed flask of air holds its gas: heated, its pressure follows its temperature (P/T constant), and it warms at constant volume (Cv), not at constant pressure (Stage 28)") {
	World::Builder b(builtin_species());
	const CellId flask = b.add_rigid_cell("flask", FLASK);
	const CellId burner = b.add_reservoir("burner", 500.0);
	const double n = P_ATMOSPHERE * FLASK / (GAS_CONSTANT * 298.15);
	b.set_amount(flask, builtin::nitrogen, mol(0.79 * n)).set_amount(flask, builtin::oxygen, mol(0.21 * n)).set_temperature(flask, 298.15);
	b.add_edge(flask, burner, PHASES_NONE, 0.05);
	std::string why;
	auto built = b.build(&why);
	REQUIRE_MESSAGE(built.has_value(), why);
	CHECK(built->meta(flask).sealed);
	const Thermal at_start = thermal_state(built->species(), built->snapshot_cell(flask), &built->meta(flask));
	REQUIRE(at_start.defined);
	CHECK(at_start.temperature == doctest::Approx(298.15).epsilon(1e-9)); // its E set as U + P_atm V at the temperature asked
	CHECK(at_start.pressure == doctest::Approx(P_ATMOSPHERE).epsilon(1e-6));
	const double cp = (0.79 * builtin_species().species(builtin::nitrogen).cp + 0.21 * builtin_species().species(builtin::oxygen).cp) * n;
	CHECK(at_start.heat_capacity == doctest::Approx(cp - n * GAS_CONSTANT).epsilon(1e-9)); // Cv = Cp - nR
	Simulation sim(std::move(*built), equilibria());
	const int64_t e0 = sim.world().snapshot_cell(flask).energy;
	run(sim, 60 * 20);
	const Thermal hot = state(sim.world(), flask);
	MESSAGE("a sealed flask of air: " << hot.temperature - 273.15 << " C at " << hot.pressure / 1000.0 << " kPa");
	CHECK(hot.temperature > 350.0);
	CHECK(hot.pressure / hot.temperature == doctest::Approx(P_ATMOSPHERE / 298.15).epsilon(1e-6));
	// the heat it took is what raised it at constant volume
	const double heat = static_cast<double>(sim.world().snapshot_cell(flask).energy - e0) * JOULE_PER_QUANTUM;
	CHECK(heat == doctest::Approx((cp - n * GAS_CONSTANT) * (hot.temperature - 298.15)).epsilon(1e-6));
}

TEST_CASE("pressure: water sealed alone with its vapour stands at its saturation pressure, as the steam tables have it from 10 kPa to 2 atm: water boils where its pressure says (NIST, IAPWS-95) (Stage 28)") {
	// NIST Chemistry WebBook, saturation properties of water by pressure (IAPWS-95), printed rows (docs/DATA_SOURCES.md, K1)
	const struct {
		double kelvin, pascal;
	} nist[] = { { 318.656, 9847.24 }, { 332.990, 19799.0 }, { 354.179, 49425.8 }, { 372.876, 100431.0 }, { 392.820, 196604.0 } };
	for (const auto &row : nist) {
		CAPTURE(row.kelvin);
		World::Builder b(builtin_species());
		// 249 mL of water in a 250 mL flask, nothing else: its vapour fills the last millilitre, and evaporating it hardly cools the water
		const CellId flask = b.add_rigid_cell("flask", FLASK);
		const double water = 249e-6 / builtin_species().species(builtin::water).molar_volume;
		b.set_amount(flask, builtin::water, mol(water)).set_temperature(flask, row.kelvin);
		std::string why;
		auto built = b.build(&why);
		REQUIRE_MESSAGE(built.has_value(), why);
		Simulation sim(std::move(*built), equilibria());
		run(sim, 3);
		const Thermal t = state(sim.world(), flask);
		CHECK(t.temperature == doctest::Approx(row.kelvin).epsilon(1e-5));
		CHECK(sim.world().amount(flask, builtin::steam).quanta > 0);
		CHECK(t.pressure == doctest::Approx(row.pascal).epsilon(0.025));
		MESSAGE((row.kelvin - 273.15) << " C: " << t.pressure << " Pa (NIST " << row.pascal << ")");
	}
}

TEST_CASE("pressure: a sealed flask of boiling water cooled falls below an atmosphere, and its water boils on at 60 C, at the saturation pressure there (Stage 28)") {
	World::Builder b(builtin_species());
	const CellId flask = b.add_rigid_cell("flask", FLASK);
	const CellId bath = b.add_reservoir("bath", 333.15);
	const CellId hand = b.add_reservoir("hand", 343.15);
	// 100 mL of water under its own steam: the air boiled out, as a flask that boils before it is stoppered
	const double steam = P_ATMOSPHERE * 150e-6 / (GAS_CONSTANT * 373.12);
	b.set_amount(flask, builtin::water, mol(100e-6 / builtin_species().species(builtin::water).molar_volume)).set_amount(flask, builtin::steam, mol(steam));
	b.set_amount(flask, builtin::glass, mol(1.0)).set_temperature(flask, 373.12);
	const EdgeId cooling = b.add_edge(flask, bath, PHASES_NONE, 20.0);
	const EdgeId warming = b.add_edge(flask, hand, PHASES_NONE, 0.0);
	std::string why;
	auto built = b.build(&why);
	REQUIRE_MESSAGE(built.has_value(), why);
	Simulation sim(std::move(*built), equilibria());
	run(sim, 60 * 300);
	const Thermal cold = state(sim.world(), flask);
	MESSAGE("cooled sealed: " << cold.temperature - 273.15 << " C at " << cold.pressure / 1000.0 << " kPa");
	CHECK(cold.temperature == doctest::Approx(333.15).epsilon(1e-3));
	CHECK(cold.pressure == doctest::Approx(19946.0).epsilon(0.03)); // NIST: 19.946 kPa at 60 C (IAPWS-95)
	// warmed by a hand at 70 C, it boils: its water goes to steam, its pressure rising with the saturation
	const int64_t steam_before = sim.world().amount(flask, builtin::steam).quanta;
	REQUIRE(sim.world().set_conductance(cooling, 0.0) == Refusal::None);
	REQUIRE(sim.world().set_conductance(warming, 20.0) == Refusal::None);
	run(sim, 60 * 10);
	CHECK(sim.world().amount(flask, builtin::steam).quanta > steam_before);
	CHECK(state(sim.world(), flask).pressure > cold.pressure);
	CHECK(state(sim.world(), flask).temperature < 343.15);
}

TEST_CASE("pressure: cells sealed together share one pressure, gas running from the higher to the lower; opened to the room, a sealed flask is at an atmosphere within the step, and cooler for its gas's expansion (Stage 28)") {
	World::Builder b(builtin_species());
	const CellId hot = b.add_rigid_cell("hot", FLASK);
	const CellId cold = b.add_rigid_cell("cold", FLASK);
	const CellId air = b.add_cell("air");
	const double n = P_ATMOSPHERE * FLASK / (GAS_CONSTANT * 298.15);
	b.set_amount(hot, builtin::nitrogen, mol(2.0 * n)).set_temperature(hot, 298.15); // two atmospheres
	b.set_amount(cold, builtin::nitrogen, mol(0.5 * n)).set_temperature(cold, 298.15); // half of one
	b.set_amount(air, builtin::nitrogen, mol(100.0)).set_temperature(air, 298.15);
	std::string why;
	auto built = b.build(&why);
	REQUIRE_MESSAGE(built.has_value(), why);
	CHECK(state(*built, hot).pressure == doctest::Approx(2.0 * P_ATMOSPHERE).epsilon(1e-6));
	Simulation sim(std::move(*built), equilibria());
	EdgeId tube;
	REQUIRE(sim.world().connect(hot, cold, PHASES_GAS, &tube) == Refusal::None);
	run(sim, 1);
	const Thermal a = state(sim.world(), hot), c = state(sim.world(), cold);
	MESSAGE("sealed together: " << a.pressure << " Pa and " << c.pressure << " Pa; " << a.temperature << " K and " << c.temperature << " K");
	CHECK(a.pressure == doctest::Approx(c.pressure).epsilon(1e-6));
	CHECK(a.pressure == doctest::Approx(1.25 * P_ATMOSPHERE).epsilon(0.01));
	CHECK(a.temperature < 298.15); // the gas left behind expanded
	CHECK(c.temperature > 298.15); // the gas that arrived was pushed in
	// opened to the room: an atmosphere, exactly full, the energy kept (the invariants, every step)
	REQUIRE(sim.world().disconnect(tube) == Refusal::None);
	const double before = state(sim.world(), hot).temperature;
	REQUIRE(sim.world().connect(hot, air, PHASES_GAS) == Refusal::None);
	CHECK_FALSE(sim.world().meta(hot).sealed);
	const double opened = state(sim.world(), hot).temperature;
	CHECK(opened < before); // read as an expansion against the room: E = U + P_atm V is H at an atmosphere
	run(sim, 1);
	CHECK(state(sim.world(), hot).pressure == P_ATMOSPHERE);
}

TEST_CASE("pressure: a cell is sealed at the room's pressure: one left under-full (starved) is refused, for its vacuum would come from nowhere (Stage 28)") {
	World::Builder b(builtin_species());
	const CellId flask = b.add_rigid_cell("flask", FLASK);
	const CellId air = b.add_cell("air");
	const double n = P_ATMOSPHERE * FLASK / (GAS_CONSTANT * 298.15);
	b.set_amount(flask, builtin::nitrogen, mol(0.5 * n)).set_temperature(flask, 298.15);
	b.set_amount(air, builtin::nitrogen, mol(1.0)).set_temperature(air, 298.15);
	const EdgeId mouth = b.add_edge(flask, air, PHASES_GAS);
	std::string why;
	auto built = b.build(&why);
	REQUIRE_MESSAGE(built.has_value(), why);
	World world = std::move(*built);
	CHECK(world.disconnect(mouth) == Refusal::WouldSeal); // half full
	CHECK_FALSE(world.meta(flask).sealed);
	Simulation sim(std::move(world), equilibria());
	run(sim, 1); // filled from the air
	CHECK(sim.world().disconnect(mouth) == Refusal::None);
	CHECK(sim.world().meta(flask).sealed);
	CHECK(state(sim.world(), flask).pressure == doctest::Approx(P_ATMOSPHERE).epsilon(1e-6));
	CHECK(state(sim.world(), flask).temperature == doctest::Approx(298.15).epsilon(1e-9));
}

namespace {

bool has(const Violations &v, const char *id) {
	for (const std::string &s : v) {
		if (s.rfind(id, 0) == 0) {
			return true;
		}
	}
	return false;
}

} // namespace

TEST_CASE("invariants: a cell's seal is checked against its edges, and room for its gas (V3); gas between cells sealed together runs from the higher pressure to the lower (T1) (Stage 28)") {
	World::Builder b(builtin_species());
	const CellId hot = b.add_rigid_cell("hot", FLASK);
	const CellId cold = b.add_rigid_cell("cold", FLASK);
	const double n = P_ATMOSPHERE * FLASK / (GAS_CONSTANT * 298.15);
	b.set_amount(hot, builtin::nitrogen, mol(2.0 * n)).set_temperature(hot, 298.15);
	b.set_amount(cold, builtin::nitrogen, mol(0.5 * n)).set_temperature(cold, 298.15);
	b.add_edge(hot, cold, PHASES_GAS);
	std::string why;
	auto built = b.build(&why);
	REQUIRE_MESSAGE(built.has_value(), why);
	Simulation sim(std::move(*built), equilibria());
	sim.set_checking(false);
	const StepTrace clean = sim.step();
	REQUIRE(check_all(builtin_species(), clean, sim.equilibria()).empty());
	REQUIRE_FALSE(clean.ledger.empty());
	{
		StepTrace t = clean; // a mark that disagrees with the edges
		t.meta[0].sealed = false;
		CHECK(has(check_all(builtin_species(), t, sim.equilibria()), "V3"));
	}
	{
		StepTrace t = clean; // no room for its gas
		t.after.cells[1].amount[static_cast<size_t>(builtin::water.index)] = mol(FLASK / builtin_species().species(builtin::water).molar_volume).quanta;
		CHECK(has(check_all(builtin_species(), t, sim.equilibria()), "V3"));
	}
	{
		StepTrace t = clean; // the gas run backwards, from the lower pressure into the higher: everything still conserved
		LedgerEntry &entry = t.ledger[0];
		REQUIRE(entry.kind == LedgerEntry::Kind::Transfer);
		const auto nitrogen = static_cast<size_t>(builtin::nitrogen.index);
		const int64_t moved = entry.delta[0].amount.quanta, e = entry.energy.quanta;
		const auto from = static_cast<size_t>(entry.from.index), to = static_cast<size_t>(entry.to.index);
		std::swap(entry.from, entry.to);
		t.after.cells[from].amount[nitrogen] += 2 * moved;
		t.after.cells[to].amount[nitrogen] -= 2 * moved;
		t.after.cells[from].energy += 2 * e;
		t.after.cells[to].energy -= 2 * e;
		const Violations v = check_all(builtin_species(), t, sim.equilibria());
		CHECK(has(v, "T1"));
		CHECK_FALSE(has(v, "C5"));
	}
}
