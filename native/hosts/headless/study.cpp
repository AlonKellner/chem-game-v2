// chem_tool study <name>: a stage's physics alone, printed against its closed
// form (GAME_SPEC §2, principle 7: each system validated in isolation). The
// studies drive the core directly; the game itself is played through the C API.
#include "chem/builtin_ids.hpp"
#include "chem/invariants.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace chem;

// Stage 2: 100 g of water in a 100 g glass flask at 80 C cools in a 20 C room
// through 2 W/K, against Newton's law of cooling.
int cooling() {
	bool ok = false;
	const SpeciesTable &table = builtin_species();
	World::Builder b(table);
	const CellId flask = b.add_cell("flask");
	const CellId room = b.add_reservoir("room", 293.15);
	b.add_edge(flask, room, PHASES_NONE, 2.0);
	b.set_amount(flask, builtin::water, amount_from_mol(100.0 / 18.015, ok));
	b.set_amount(flask, builtin::glass, amount_from_mol(100.0 / 60.083, ok));
	b.set_temperature(flask, 353.15);
	std::string why;
	auto world = b.build(&why);
	if (!world) {
		std::fprintf(stderr, "chem_tool: %s\n", why.c_str());
		return 1;
	}
	Simulation sim(std::move(*world));
	sim.set_checking(true);
	const double capacity = thermal_state(table, sim.world().snapshot_cell(flask)).heat_capacity;
	double start = 0.0;
	sim.world().temperature(flask, start);
	const Totals genesis = totals(table, sim.world().snapshot());

	std::printf("A flask (100 g water, 100 g glass, %.2f J/K) at 80 C in a 20 C room, 2 W/K; time constant %.1f s\n\n", capacity, capacity / 2.0);
	std::printf("%8s %14s %14s %12s %18s %18s %14s\n", "time s", "simulated C", "Newton's law C", "error K", "flask energy J", "room energy J", "entropy J/K");
	double entropy_made = 0.0;
	double worst = 0.0;
	for (int step = 0; step <= 54000; ++step) {
		if (step % 3600 == 0) {
			double t = 0.0;
			sim.world().temperature(flask, t);
			const double analytic = 293.15 + (start - 293.15) * std::exp(-2.0 * step * STEP_SECONDS / capacity);
			worst = std::fmax(worst, std::fabs(t - analytic));
			std::printf("%8.0f %14.9f %14.9f %12.2e %18.9f %18.9f %14.9f\n", step * STEP_SECONDS, t - 273.15, analytic - 273.15, t - analytic,
					sim.world().energy(flask).joule(), sim.world().energy(room).joule(), entropy_made);
		}
		entropy_made += entropy_change(table, sim.step());
	}
	const Totals end = totals(table, sim.world().snapshot());
	std::printf("\nworst error against Newton's law: %.2e K\n", worst);
	std::printf("world energy: %lld quanta at the start, %lld at the end (%s)\n", static_cast<long long>(genesis.energy), static_cast<long long>(end.energy),
			genesis == end ? "every total identical" : "TOTALS MOVED");
	std::printf("entropy made: %.6f J/K (never negative in any step: invariant T2 was checked on all 54001)\n", entropy_made);

	// the cost of a step, checking off
	Simulation timed(sim);
	timed.set_checking(false);
	const auto t0 = std::chrono::steady_clock::now();
	const int steps = 200000;
	for (int i = 0; i < steps; ++i) {
		timed.step();
	}
	const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / steps;
	std::printf("step cost (this world, checks off, trace built): %.0f ns\n", ns);
	return genesis == end ? 0 : 1;
}

// Stage 3: 5 g of malachite in a 100 g glass flask of air over a flame. One
// sealed cell: the gases stay in it (they leave for the balloon in Stage 4).
int decomposition() {
	bool ok = false;
	const SpeciesTable &table = builtin_species();
	World::Builder b(table);
	const CellId flask = b.add_cell("flask");
	const CellId flame = b.add_reservoir("flame", 2200.0);
	const CellId room = b.add_reservoir("room", 293.15);
	b.add_edge(flask, flame, PHASES_NONE, 0.2);
	b.add_edge(flask, room, PHASES_NONE, 0.9);
	b.set_amount(flask, builtin::malachite, amount_from_mol(5.0 / 221.114, ok));
	b.set_amount(flask, builtin::glass, amount_from_mol(100.0 / 60.083, ok));
	b.set_amount(flask, builtin::nitrogen, amount_from_mol(0.0080, ok));
	b.set_amount(flask, builtin::oxygen, amount_from_mol(0.0022, ok));
	b.set_temperature(flask, 293.15);
	std::string why;
	auto world = b.build(&why);
	if (!world) {
		std::fprintf(stderr, "chem_tool: %s\n", why.c_str());
		return 1;
	}
	Simulation sim(std::move(*world), std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end()));
	sim.set_checking(true);
	const Totals genesis = totals(table, sim.world().snapshot());
	const auto grams = [&](SpeciesId s) { return sim.world().amount(flask, s).mol() * static_cast<double>(table.molar_mass_ug(s)) * 1e-6; };

	std::printf("5 g of malachite in a 100 g glass flask of air; a 2200 K flame through 0.2 W/K, a 20 C room through 0.9 W/K\n");
	std::printf("Cu2CO3(OH)2(s) -> 2 CuO(s) + CO2(g) + H2O(g), first order, Ea 150 kJ/mol\n\n");
	std::printf("%7s %9s %12s %9s %9s %9s %14s %14s %12s\n", "time s", "flask C", "malachite g", "CuO g", "CO2 g", "H2O g", "from flame J", "to room J", "entropy J/K");
	double entropy_made = 0.0;
	for (int step = 0; step <= 72000; ++step) {
		if (step % 3600 == 0) {
			double t = 0.0;
			sim.world().temperature(flask, t);
			std::printf("%7.0f %9.2f %12.6f %9.6f %9.6f %9.6f %14.3f %14.3f %12.4f\n", step * STEP_SECONDS, t - 273.15, grams(builtin::malachite),
					grams(builtin::copper_oxide), grams(builtin::carbon_dioxide), grams(builtin::steam), 0.0 - sim.world().energy(flame).joule(),
					sim.world().energy(room).joule(), entropy_made);
		}
		entropy_made += entropy_change(table, sim.step());
	}
	const Totals end = totals(table, sim.world().snapshot());
	std::printf("\nmass in the world: %.9f g at the start, %.9f g at the end; every element, the mass and the energy %s\n", genesis.mass.grams(), end.mass.grams(),
			genesis == end ? "are the same integers" : "MOVED");
	std::printf("every invariant (C1-C8, T1-T3, S1) was checked on all 72001 steps\n");

	Simulation timed(sim);
	timed.set_checking(false);
	const auto t0 = std::chrono::steady_clock::now();
	const int steps = 200000;
	for (int i = 0; i < steps; ++i) {
		timed.step();
	}
	const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / steps;
	std::printf("step cost (this world: one cell, two reservoirs, seven reactions offered; checks off, trace built): %.0f ns\n", ns);
	return genesis == end ? 0 : 1;
}

// Stage 4: the demo's first stage without its player. 5 g of malachite in a
// 250 mL flask of air with a balloon on its mouth; twenty minutes over the
// flame, then twenty with the burner off.
int balloon() {
	bool ok = false;
	const SpeciesTable &table = builtin_species();
	const double volume = 250e-6;
	const double malachite = 5.0 / 221.114;
	World::Builder b(table);
	const CellId flask = b.add_rigid_cell("flask", volume);
	const CellId bag = b.add_cell("balloon");
	const CellId flame = b.add_reservoir("flame", 2200.0);
	const CellId room = b.add_reservoir("room", 293.15);
	b.add_edge(flask, bag, PHASES_GAS);
	const EdgeId burner = b.add_edge(flask, flame, PHASES_NONE, 0.2);
	b.add_edge(flask, room, PHASES_NONE, 0.9);
	b.add_edge(bag, room, PHASES_NONE, 0.5);
	const double air = P_ATMOSPHERE * (volume - malachite * table.species(builtin::malachite).molar_volume) / (GAS_CONSTANT * 293.15);
	b.set_amount(flask, builtin::malachite, amount_from_mol(malachite, ok));
	b.set_amount(flask, builtin::glass, amount_from_mol(100.0 / 60.083, ok));
	b.set_amount(flask, builtin::nitrogen, amount_from_mol(0.79 * air, ok));
	b.set_amount(flask, builtin::oxygen, amount_from_mol(0.21 * air, ok));
	b.set_temperature(flask, 293.15);
	b.set_amount(bag, builtin::latex, amount_from_mol(2.0 / 68.119, ok));
	b.set_temperature(bag, 293.15);
	std::string why;
	auto world = b.build(&why);
	if (!world) {
		std::fprintf(stderr, "chem_tool: %s\n", why.c_str());
		return 1;
	}
	Simulation sim(std::move(*world), std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end()));
	sim.set_checking(true);
	const Totals genesis = totals(table, sim.world().snapshot());
	const auto mol = [&](CellId c, SpeciesId s) { return sim.world().amount(c, s).mol(); };
	const auto celsius = [&](CellId c) {
		double t = 0.0;
		sim.world().temperature(c, t);
		return t - 273.15;
	};

	std::printf("5 g of malachite in a 250 mL flask of air (100 g of glass), a 2 g balloon on its mouth\n");
	std::printf("flame 2200 K through 0.2 W/K for 20 minutes, then off; the room at 20 C takes 0.9 W/K from the flask, 0.5 W/K from the balloon\n\n");
	std::printf("%7s %8s %10s %12s %10s %8s %8s %8s %13s\n", "time s", "flask C", "balloon C", "malachite g", "balloon L", "CO2 %", "H2O %", "air %", "flask gas mol");
	for (int step = 0; step <= 144000; ++step) {
		if (step == 72000) {
			sim.world().set_conductance(burner, 0.0);
			std::printf("%7s burner off\n", "");
		}
		if (step % 7200 == 0) {
			const double co2 = mol(bag, builtin::carbon_dioxide), h2o = mol(bag, builtin::steam);
			const double n2o2 = mol(bag, builtin::nitrogen) + mol(bag, builtin::oxygen);
			const double all = co2 + h2o + n2o2;
			double t = 0.0;
			sim.world().temperature(bag, t);
			const double litres = fill_of(table, sim.world().snapshot_cell(bag), t).gas * 1e3;
			const double flask_gas = mol(flask, builtin::carbon_dioxide) + mol(flask, builtin::steam) + mol(flask, builtin::nitrogen) + mol(flask, builtin::oxygen);
			std::printf("%7.0f %8.2f %10.2f %12.6f %10.4f %8.2f %8.2f %8.2f %13.6f\n", step * STEP_SECONDS, celsius(flask), celsius(bag),
					mol(flask, builtin::malachite) * 221.114, litres, all > 0 ? 100 * co2 / all : 0.0, all > 0 ? 100 * h2o / all : 0.0,
					all > 0 ? 100 * n2o2 / all : 0.0, flask_gas);
		}
		sim.step();
	}
	const Totals end = totals(table, sim.world().snapshot());
	std::printf("\nCO2 made: %.6f g; in the balloon: %.6f g; left in the flask: %.6f g\n",
			(mol(bag, builtin::carbon_dioxide) + mol(flask, builtin::carbon_dioxide)) * 44.009, mol(bag, builtin::carbon_dioxide) * 44.009,
			mol(flask, builtin::carbon_dioxide) * 44.009);
	std::printf("the water is still vapour at 20 C here: condensation is Stage 5\n");
	std::printf("every element, the mass and the energy %s; every invariant was checked on all 144001 steps\n",
			genesis == end ? "are the same integers as at the start" : "MOVED");

	Simulation timed(sim);
	timed.set_checking(false);
	const auto t0 = std::chrono::steady_clock::now();
	const int steps = 200000;
	for (int i = 0; i < steps; ++i) {
		timed.step();
	}
	const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / steps;
	std::printf("step cost (two cells, two reservoirs, the reaction table; checks off, trace built): %.0f ns\n", ns);
	return genesis == end ? 0 : 1;
}

} // namespace

int run_study(const char *name) {
	if (std::strcmp(name, "cooling") == 0) {
		return cooling();
	}
	if (std::strcmp(name, "decomposition") == 0) {
		return decomposition();
	}
	if (std::strcmp(name, "balloon") == 0) {
		return balloon();
	}
	std::fprintf(stderr, "chem_tool study: unknown study \"%s\" (cooling, decomposition, balloon)\n", name);
	return 2;
}
