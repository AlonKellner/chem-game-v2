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
	std::printf("water condensed inside the balloon: %.6f g of the %.6f g made\n", mol(bag, builtin::water) * 18.015,
			(mol(bag, builtin::water) + mol(bag, builtin::steam) + mol(flask, builtin::water) + mol(flask, builtin::steam)) * 18.015);
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

// Stage 5: the demo's third stage without its player or its salt. 100 g of
// water in a flask over a flame, a condenser over a beaker, the open air.
int distillation() {
	bool ok = false;
	const SpeciesTable &table = builtin_species();
	World::Builder b(table);
	const CellId flask = b.add_rigid_cell("flask", 250e-6);
	const CellId condenser = b.add_rigid_cell("condenser", 50e-6);
	const CellId beaker = b.add_rigid_cell("beaker", 250e-6);
	const CellId air = b.add_cell("air");
	const CellId flame = b.add_reservoir("flame", 2200.0);
	const CellId coolant = b.add_reservoir("coolant", 288.15);
	const CellId room = b.add_reservoir("room", 293.15);
	b.add_edge(flask, condenser, PHASES_GAS);
	b.add_edge(condenser, air, PHASES_GAS);
	b.add_edge(beaker, air, PHASES_GAS);
	b.add_drain(condenser, beaker);
	b.add_edge(flask, flame, PHASES_NONE, 0.25);
	b.add_edge(flask, room, PHASES_NONE, 0.9);
	b.add_edge(condenser, coolant, PHASES_NONE, 10.0);
	b.add_edge(beaker, room, PHASES_NONE, 0.5);
	b.add_edge(air, room, PHASES_NONE, 1000.0);
	const double liquid = 100.0 / 18.015;
	const auto air_in = [&](CellId cell, double volume) {
		const double n = P_ATMOSPHERE * volume / (GAS_CONSTANT * 293.15);
		b.set_amount(cell, builtin::nitrogen, amount_from_mol(0.79 * n, ok)).set_amount(cell, builtin::oxygen, amount_from_mol(0.21 * n, ok));
	};
	b.set_amount(flask, builtin::water, amount_from_mol(liquid, ok)).set_amount(flask, builtin::glass, amount_from_mol(100.0 / 60.083, ok));
	air_in(flask, 250e-6 - liquid * table.species(builtin::water).molar_volume);
	b.set_temperature(flask, 293.15);
	b.set_amount(condenser, builtin::glass, amount_from_mol(50.0 / 60.083, ok));
	air_in(condenser, 50e-6);
	b.set_temperature(condenser, 293.15);
	b.set_amount(beaker, builtin::glass, amount_from_mol(80.0 / 60.083, ok));
	air_in(beaker, 250e-6);
	b.set_temperature(beaker, 293.15);
	b.set_amount(air, builtin::nitrogen, amount_from_mol(790.0, ok)).set_amount(air, builtin::oxygen, amount_from_mol(210.0, ok));
	b.set_temperature(air, 293.15);
	std::string why;
	auto world = b.build(&why);
	if (!world) {
		std::fprintf(stderr, "chem_tool: %s\n", why.c_str());
		return 1;
	}
	Simulation sim(std::move(*world), std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end()));
	sim.set_checking(true);
	const Totals genesis = totals(table, sim.world().snapshot());
	const auto grams = [&](CellId c, SpeciesId s) { return sim.world().amount(c, s).mol() * 18.015; };
	const auto celsius = [&](CellId c) {
		double t = 0.0;
		sim.world().temperature(c, t);
		return t - 273.15;
	};
	const auto vapour = [&](CellId c) {
		const double w = sim.world().amount(c, builtin::steam).mol();
		const double all = w + sim.world().amount(c, builtin::nitrogen).mol() + sim.world().amount(c, builtin::oxygen).mol();
		return all > 0.0 ? 100.0 * w / all : 0.0;
	};

	std::printf("100 g of water in a 250 mL flask; flame 2200 K through 0.25 W/K; a 50 mL condenser, coolant 15 C through 10 W/K; a beaker under its outlet\n\n");
	std::printf("%7s %8s %14s %12s %12s %14s %10s %14s %16s\n", "time s", "flask C", "flask water g", "flask vap %", "condenser C", "condenser vap %", "beaker C", "beaker water g",
			"vapour to air g");
	for (int step = 0; step <= 46800; ++step) {
		if (step % 3600 == 0) {
			std::printf("%7.0f %8.2f %14.4f %12.2f %12.2f %14.2f %10.2f %14.4f %16.6f\n", step * STEP_SECONDS, celsius(flask), grams(flask, builtin::water), vapour(flask),
					celsius(condenser), vapour(condenser), celsius(beaker), grams(beaker, builtin::water), grams(air, builtin::steam) + grams(air, builtin::water));
		}
		sim.step();
	}
	const Totals end = totals(table, sim.world().snapshot());
	const double boiled = 100.0 - grams(flask, builtin::water) - grams(flask, builtin::steam);
	std::printf("\nleft the flask: %.4f g; in the beaker as liquid: %.4f g (%.2f%%); the rest is vapour in the condenser, the beaker and the air\n", boiled,
			grams(beaker, builtin::water), 100.0 * grams(beaker, builtin::water) / boiled);
	std::printf("the coolant took %.1f kJ; the flame gave %.1f kJ\n", sim.world().energy(coolant).joule() / 1000.0, (0.0 - sim.world().energy(flame).joule()) / 1000.0);
	std::printf("every element, the mass and the energy %s; every invariant (now with P1 and V2) was checked on all 46801 steps\n",
			genesis == end ? "are the same integers as at the start" : "MOVED");

	Simulation timed(sim);
	timed.set_checking(false);
	const auto t0 = std::chrono::steady_clock::now();
	const int steps = 200000;
	for (int i = 0; i < steps; ++i) {
		timed.step();
	}
	const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / steps;
	std::printf("step cost (four cells, three reservoirs, the reaction table; checks off, trace built): %.0f ns\n", ns);
	return genesis == end ? 0 : 1;
}

std::vector<Reaction> shipped() {
	return std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end());
}

double celsius_of(const World &world, CellId cell) {
	double t = 0.0;
	world.temperature(cell, t);
	return t - 273.15;
}

// mol of a dissolved species per kg of the cell's water
double molality(const World &world, CellId cell, SpeciesId solute) {
	const double kg = world.amount(cell, builtin::water).mol() * 0.018015;
	return kg > 0.0 ? world.amount(cell, solute).mol() / kg : 0.0;
}

// Stage 6: the demo's second stage without its player. 3.6 g of CuO powder
// (the malachite's 2.5 million grains) under 100 mL of 1 M sulfuric acid, in
// an open flask: cold and still, cold and stirred, warm and stirred.
int dissolution() {
	bool ok = false;
	const SpeciesTable &table = builtin_species();
	std::printf("3.597 g of CuO in 2.5 million grains under 100 mL of 1 M sulfuric acid (0.1 mol in 96.2 g of water), a 250 mL flask open to the air\n");
	std::printf("CuO(s) + H2SO4(aq) -> CuSO4(aq) + H2O(l), at the oxide's surface, proportional to the acid's molality and to the stirring\n\n");
	const struct {
		const char *name;
		double start, mixing, flame;
	} runs[] = { { "20 C, still", 293.15, 1.0, 0.0 }, { "20 C, stirred (x3)", 293.15, 3.0, 0.0 }, { "gentle flame, stirred (x3)", 293.15, 3.0, 0.05 } };
	int status = 0;
	for (const auto &run : runs) {
		World::Builder b(table);
		const CellId flask = b.add_rigid_cell("flask", 250e-6);
		const CellId air = b.add_cell("air");
		const CellId flame = b.add_reservoir("flame", 2200.0);
		const CellId room = b.add_reservoir("room", 293.15);
		b.add_edge(flask, air, PHASES_GAS);
		b.add_edge(flask, flame, PHASES_NONE, run.flame);
		b.add_edge(flask, room, PHASES_NONE, 0.9);
		b.add_edge(air, room, PHASES_NONE, 1000.0);
		const double oxide = 3.597 / 79.545, water = 96.2 / 18.015;
		b.set_amount(flask, builtin::copper_oxide, amount_from_mol(oxide, ok)).set_particles(flask, builtin::copper_oxide, 2500000);
		b.set_amount(flask, builtin::water, amount_from_mol(water, ok)).set_amount(flask, builtin::sulfuric_acid_aq, amount_from_mol(0.1, ok));
		b.set_amount(flask, builtin::glass, amount_from_mol(100.0 / 60.083, ok));
		const double gas = P_ATMOSPHERE * (250e-6 - water * table.species(builtin::water).molar_volume - oxide * table.species(builtin::copper_oxide).molar_volume) /
				(GAS_CONSTANT * run.start);
		b.set_amount(flask, builtin::nitrogen, amount_from_mol(0.79 * gas, ok)).set_amount(flask, builtin::oxygen, amount_from_mol(0.21 * gas, ok));
		b.set_temperature(flask, run.start);
		b.set_amount(air, builtin::nitrogen, amount_from_mol(790.0, ok)).set_amount(air, builtin::oxygen, amount_from_mol(210.0, ok));
		b.set_temperature(air, 293.15);
		std::string why;
		auto world = b.build(&why);
		if (!world) {
			std::fprintf(stderr, "chem_tool: %s\n", why.c_str());
			return 1;
		}
		world->set_mixing(flask, run.mixing);
		Simulation sim(std::move(*world), shipped());
		sim.set_checking(true);
		const Totals genesis = totals(table, sim.world().snapshot());
		std::printf("%s\n%8s %8s %10s %14s %14s %12s\n", run.name, "time s", "flask C", "CuO g", "CuSO4 mol/kg", "acid mol/kg", "grain um");
		double half = 0.0, done = 0.0;
		for (int step = 0; step <= 60 * 3600; ++step) {
			const double left = sim.world().amount(flask, builtin::copper_oxide).mol() / oxide;
			if (half == 0.0 && left <= 0.5) {
				half = step * STEP_SECONDS;
			}
			if (done == 0.0 && left <= 0.01) {
				done = step * STEP_SECONDS;
			}
			if (step % (60 * 300) == 0 && (done == 0.0 || step * STEP_SECONDS < done + 300.0)) {
				std::printf("%8.0f %8.2f %10.5f %14.5f %14.5f %12.3f\n", step * STEP_SECONDS, celsius_of(sim.world(), flask), left * 3.597,
						molality(sim.world(), flask, builtin::copper_sulfate_aq), molality(sim.world(), flask, builtin::sulfuric_acid_aq),
						particle_diameter(table, sim.world().snapshot_cell(flask), builtin::copper_oxide) * 1e6);
			}
			sim.step();
		}
		std::printf("half dissolved at %.0f s, 99%% at %.0f s; totals %s\n\n", half, done, totals(table, sim.world().snapshot()) == genesis ? "unchanged" : "MOVED");
		status |= totals(table, sim.world().snapshot()) == genesis ? 0 : 1;
	}
	return status;
}

// Stage 6: the demo's third stage without its player. The solution of stage 2
// is boiled down through the condenser, then left to cool.
int crystals() {
	bool ok = false;
	const SpeciesTable &table = builtin_species();
	World::Builder b(table);
	const CellId flask = b.add_rigid_cell("flask", 250e-6);
	const CellId condenser = b.add_rigid_cell("condenser", 50e-6);
	const CellId beaker = b.add_rigid_cell("beaker", 250e-6);
	const CellId air = b.add_cell("air");
	const CellId flame = b.add_reservoir("flame", 2200.0);
	const CellId coolant = b.add_reservoir("coolant", 288.15);
	const CellId room = b.add_reservoir("room", 293.15);
	b.add_edge(flask, condenser, PHASES_GAS);
	b.add_edge(condenser, air, PHASES_GAS);
	b.add_edge(beaker, air, PHASES_GAS);
	b.add_drain(condenser, beaker);
	const EdgeId burner = b.add_edge(flask, flame, PHASES_NONE, 0.25);
	b.add_edge(flask, room, PHASES_NONE, 0.9);
	b.add_edge(condenser, coolant, PHASES_NONE, 10.0);
	b.add_edge(beaker, room, PHASES_NONE, 0.5);
	b.add_edge(air, room, PHASES_NONE, 1000.0);
	// stage 2's flask: 0.0452 mol of CuO dissolved in 100 mL of 1 M acid
	const double water = (96.2 + 0.8146) / 18.015, salt = 3.597 / 79.545, acid = 0.1 - salt;
	const auto air_in = [&](CellId cell, double volume) {
		const double n = P_ATMOSPHERE * volume / (GAS_CONSTANT * 293.15);
		b.set_amount(cell, builtin::nitrogen, amount_from_mol(0.79 * n, ok)).set_amount(cell, builtin::oxygen, amount_from_mol(0.21 * n, ok));
	};
	b.set_amount(flask, builtin::water, amount_from_mol(water, ok)).set_amount(flask, builtin::copper_sulfate_aq, amount_from_mol(salt, ok));
	b.set_amount(flask, builtin::sulfuric_acid_aq, amount_from_mol(acid, ok)).set_amount(flask, builtin::glass, amount_from_mol(100.0 / 60.083, ok));
	air_in(flask, 250e-6 - water * table.species(builtin::water).molar_volume);
	b.set_temperature(flask, 293.15);
	b.set_amount(condenser, builtin::glass, amount_from_mol(50.0 / 60.083, ok));
	air_in(condenser, 50e-6);
	b.set_temperature(condenser, 293.15);
	b.set_amount(beaker, builtin::glass, amount_from_mol(80.0 / 60.083, ok));
	air_in(beaker, 250e-6);
	b.set_temperature(beaker, 293.15);
	b.set_amount(air, builtin::nitrogen, amount_from_mol(790.0, ok)).set_amount(air, builtin::oxygen, amount_from_mol(210.0, ok));
	b.set_temperature(air, 293.15);
	std::string why;
	auto world = b.build(&why);
	if (!world) {
		std::fprintf(stderr, "chem_tool: %s\n", why.c_str());
		return 1;
	}
	Simulation sim(std::move(*world), shipped());
	sim.set_checking(true);
	const Totals genesis = totals(table, sim.world().snapshot());
	const auto grams = [&](CellId c, SpeciesId s) { return sim.world().amount(c, s).mol() * static_cast<double>(table.molar_mass_ug(s)) * 1e-6; };

	std::printf("stage 2's flask: %.4f mol of CuSO4 and %.4f mol of sulfuric acid in %.1f g of water; flame 2200 K through 0.25 W/K; condenser and beaker\n", salt, acid,
			water * 18.015);
	std::printf("the burner goes off when 14 g of water is left in the flask\n\n");
	std::printf("%7s %8s %13s %14s %13s %12s %12s %14s %12s\n", "time s", "flask C", "flask water g", "CuSO4 mol/kg", "crystals g", "crystals", "size um", "anhydrous g",
			"beaker g");
	bool lit = true;
	for (int step = 0; step <= 60 * 3600; ++step) {
		if (lit && grams(flask, builtin::water) < 14.0) {
			sim.world().set_conductance(burner, 0.0);
			lit = false;
			std::printf("%7.0f burner off\n", step * STEP_SECONDS);
		}
		if (step % (60 * 180) == 0) {
			std::printf("%7.0f %8.2f %13.4f %14.4f %13.5f %12lld %12.2f %14.5f %12.4f\n", step * STEP_SECONDS, celsius_of(sim.world(), flask), grams(flask, builtin::water),
					molality(sim.world(), flask, builtin::copper_sulfate_aq), grams(flask, builtin::copper_sulfate_pentahydrate),
					static_cast<long long>(sim.world().particles(flask, builtin::copper_sulfate_pentahydrate)),
					particle_diameter(table, sim.world().snapshot_cell(flask), builtin::copper_sulfate_pentahydrate) * 1e6, grams(flask, builtin::copper_sulfate),
					grams(beaker, builtin::water));
		}
		sim.step();
	}
	const Totals end = totals(table, sim.world().snapshot());
	const double crop = grams(flask, builtin::copper_sulfate_pentahydrate);
	std::printf("\ncrystals: %.4f g of CuSO4.5H2O in %lld crystals of %.1f um (%.1f%% of the copper); the most the copper could give is %.4f g\n", crop,
			static_cast<long long>(sim.world().particles(flask, builtin::copper_sulfate_pentahydrate)),
			particle_diameter(table, sim.world().snapshot_cell(flask), builtin::copper_sulfate_pentahydrate) * 1e6, 100.0 * crop / (salt * 249.677), salt * 249.677);
	std::printf("mother liquor: %.4f g of water, %.4f mol/kg CuSO4, %.4f mol/kg sulfuric acid\n", grams(flask, builtin::water), molality(sim.world(), flask, builtin::copper_sulfate_aq),
			molality(sim.world(), flask, builtin::sulfuric_acid_aq));
	std::printf("beaker: %.4f g of water and no other liquid or solid\n", grams(beaker, builtin::water));
	std::printf("every element, the mass and the energy %s; every invariant was checked on all %d steps\n", genesis == end ? "are the same integers as at the start" : "MOVED",
			60 * 3600 + 1);
	return genesis == end ? 0 : 1;
}

// Stage 41: the heat of solution (GAME_SPEC §4.2, §13.7). A salt tipped into a
// beaker of water and stirred: the anhydrous salt warms it, the pentahydrate
// cools it. Nothing is added by hand: the enthalpies of formation decide, and
// the closed form is the beaker's heat capacity taking the heat of solution.
int solution_heat() {
	bool ok = false;
	const SpeciesTable &table = builtin_species();
	const auto hf = [&](SpeciesId s) { return table.species(s).hf; };
	const struct {
		const char *name;
		SpeciesId salt;
		int waters;
		double grams;
	} runs[] = { { "anhydrous CuSO4", builtin::copper_sulfate, 0, 8.0 }, { "CuSO4.5H2O", builtin::copper_sulfate_pentahydrate, 5, 12.5 } };
	std::printf("a salt tipped into 50 g of water in an 80 g glass beaker at 20 C, stirred (x3); adiabatic, then losing 0.5 W/K to a 20 C room\n\n");
	int status = 0;
	for (const auto &run : runs) {
		const double n = run.grams / (static_cast<double>(table.molar_mass_ug(run.salt)) * 1e-6);
		const auto cp = [&](SpeciesId s) { return table.species(s).cp; };
		const double dissolve = hf(builtin::copper_sulfate_aq) + run.waters * hf(builtin::water) - hf(run.salt); // J/mol, at 25 C
		// at 20 C: the heat capacities are constant (§10), so the heat of solution moves by their change
		const double at_20 = dissolve + (cp(builtin::copper_sulfate_aq) + run.waters * cp(builtin::water) - cp(run.salt)) * (293.15 - 298.15);
		for (const double loss : { 0.0, 0.5 }) {
			World::Builder b(table);
			const CellId beaker = b.add_rigid_cell("beaker", 250e-6);
			const CellId air = b.add_cell("air");
			const CellId room = b.add_reservoir("room", 293.15);
			b.add_edge(beaker, air, PHASES_GAS);
			if (loss > 0.0) {
				b.add_edge(beaker, room, PHASES_NONE, loss);
			}
			const double water = 50.0 / 18.015;
			b.set_amount(beaker, builtin::water, amount_from_mol(water, ok)).set_amount(beaker, builtin::glass, amount_from_mol(80.0 / 60.083, ok));
			b.set_amount(beaker, run.salt, amount_from_mol(n, ok)).set_particles(beaker, run.salt, 2000000);
			const double gas = P_ATMOSPHERE * (250e-6 - water * table.species(builtin::water).molar_volume - n * table.species(run.salt).molar_volume) / (GAS_CONSTANT * 293.15);
			b.set_amount(beaker, builtin::nitrogen, amount_from_mol(0.79 * gas, ok)).set_amount(beaker, builtin::oxygen, amount_from_mol(0.21 * gas, ok));
			b.set_temperature(beaker, 293.15);
			b.set_amount(air, builtin::nitrogen, amount_from_mol(790.0, ok)).set_amount(air, builtin::oxygen, amount_from_mol(210.0, ok));
			b.set_temperature(air, 293.15);
			b.set_mixing(beaker, 3.0);
			std::string why;
			auto world = b.build(&why);
			if (!world) {
				std::fprintf(stderr, "chem_tool: %s\n", why.c_str());
				return 1;
			}
			Simulation sim(std::move(*world), shipped());
			sim.set_checking(true);
			const Totals genesis = totals(table, sim.world().snapshot());
			std::printf("%s, %.4f mol (%.1f g); heat of solution from the tables %+.2f kJ/mol at 25 C, %+.2f at 20 C; %s\n", run.name, n, run.grams, dissolve * 1e-3, at_20 * 1e-3,
					loss > 0.0 ? "losing heat to the room" : "adiabatic");
			std::printf("%8s %9s %10s %14s %14s %14s\n", "time s", "beaker C", "salt g", "CuSO4 mol/kg", "closed form C", "other solids g");
			double extreme = 20.0, half = 0.0, done = 0.0;
			for (int step = 0; step <= 60 * 1800; ++step) {
				const double c = celsius_of(sim.world(), beaker);
				extreme = run.waters == 0 ? std::fmax(extreme, c) : std::fmin(extreme, c);
				const double left = sim.world().amount(beaker, run.salt).mol() / n;
				if (half == 0.0 && left <= 0.5) {
					half = step * STEP_SECONDS;
				}
				if (done == 0.0 && left <= 0.01) {
					done = step * STEP_SECONDS;
				}
				if (step % (60 * 60) == 0 && (step <= 60 * 600 || step % (60 * 300) == 0)) {
					// the closed form: the heat the dissolved part took, over what the beaker holds now
					const double dissolved = n - sim.world().amount(beaker, run.salt).mol();
					const double capacity = thermal_state(table, sim.world().snapshot_cell(beaker)).heat_capacity;
					double others = 0.0;
					for (const SpeciesId s : { builtin::copper_sulfate, builtin::copper_sulfate_monohydrate, builtin::copper_sulfate_trihydrate,
								 builtin::copper_sulfate_pentahydrate }) {
						if (s != run.salt) {
							others += sim.world().amount(beaker, s).mol() * static_cast<double>(table.molar_mass_ug(s)) * 1e-6;
						}
					}
					std::printf("%8.0f %9.3f %10.4f %14.5f %14.3f %14.5f\n", step * STEP_SECONDS, c, left * run.grams, molality(sim.world(), beaker, builtin::copper_sulfate_aq),
							20.0 - dissolved * at_20 / capacity, others);
				}
				sim.step();
			}
			const bool same = totals(table, sim.world().snapshot()) == genesis;
			std::printf("%s %.3f C (%+.3f K); half dissolved at %.0f s, 99%% at %.0f s; totals %s\n", run.waters == 0 ? "warmest" : "coldest", extreme, extreme - 20.0, half,
					done, same ? "unchanged" : "MOVED");
			status |= same ? 0 : 1;
			if (loss == 0.0) {
				// adiabatic: all of it dissolved, the beaker is where the closed form puts it (within the little the
				// steam and the air over the water take: 0.1 K)
				const double capacity = thermal_state(table, sim.world().snapshot_cell(beaker)).heat_capacity;
				const double closed = 20.0 - n * at_20 / capacity, off = celsius_of(sim.world(), beaker) - closed;
				std::printf("closed form %.3f C: off by %+.3f K (%s)\n", closed, off, std::fabs(off) < 0.1 && done > 0.0 ? "within 0.1 K" : "MORE THAN 0.1 K, OR NOT ALL DISSOLVED");
				status |= std::fabs(off) < 0.1 && done > 0.0 ? 0 : 1;
			}
			std::printf("\n");
		}
	}
	return status;
}

// (Stage 63) the heat a step's change of composition gives out at a temperature, from the tables alone: the sum
// over every species of its change times its enthalpy there (negative: given out)
double heat_of_change(const SpeciesTable &table, const CellState &before, const CellState &after, double kelvin) {
	double h = 0.0;
	for (size_t s = 0; s < before.amount.size(); ++s) {
		const SpeciesDef &d = table.species(SpeciesId{ static_cast<int32_t>(s) });
		h += static_cast<double>(after.amount[s] - before.amount[s]) * MOL_PER_QUANTUM * (d.hf + d.cp * (kelvin - T0));
	}
	return h;
}

// Stage 43: a titration (GAME_SPEC §9). 25 mL of 1 M sulfuric acid and 1 M sodium hydroxide added to it, mixed
// at 20 C and settled in a step: the pH against what is left over of either, the warmth against the heat of the
// waters made. The neutralisation is a fast equilibrium, so each mixture is the step's end.
int neutralisation() {
	bool ok = false;
	const SpeciesTable &table = builtin_species();
	const auto hf = [&](SpeciesId x) { return table.species(x).hf; };
	const auto cp = [&](SpeciesId x) { return table.species(x).cp; };
	const double heat = hf(builtin::sodium_sulfate_aq) + 2.0 * hf(builtin::water) - hf(builtin::sulfuric_acid_aq) - 2.0 * hf(builtin::sodium_hydroxide_aq) +
			(cp(builtin::sodium_sulfate_aq) + 2.0 * cp(builtin::water) - cp(builtin::sulfuric_acid_aq) - 2.0 * cp(builtin::sodium_hydroxide_aq)) * (293.15 - T0);
	std::printf("25 mL of 1 M sulfuric acid (0.025 mol in 24.05 g of water), 1 M sodium hydroxide (0.001 mol in 0.96 g of water a mL) added, mixed at 20 C\n");
	std::printf("H2SO4(aq) + 2 NaOH(aq) -> Na2SO4(aq) + 2 H2O(l): %.2f kJ at 20 C, %.2f kJ a water; Kw %.4g at 20 C (pKw %.3f)\n\n", heat * 1e-3, heat * 5e-4,
			water_ionisation(table, 293.15), -std::log10(water_ionisation(table, 293.15)));
	std::printf("(in ions, Stage 63: H+ + OH- -> H2O, %.2f kJ a water; the acid's second hydrogen held in part as bisulfate)\n\n",
			(hf(builtin::water) - hf(builtin::hydrogen_ion) - hf(builtin::hydroxide_ion)) * 1e-3);
	std::printf("%8s %10s %12s %12s %10s %12s %10s\n", "base mL", "pH", "ideal pH", "C", "closed C", "HSO4- mol", "invariants");
	int status = 0;
	for (int ml = 0; ml <= 70; ml += (ml >= 44 && ml < 56) ? 1 : 4) {
		const double base = 0.001 * ml, water = (24.05 + 0.96 * ml) / 18.015;
		World::Builder b(table);
		const CellId beaker = b.add_cell("beaker");
		b.set_amount(beaker, builtin::water, amount_from_mol(water, ok)).set_amount(beaker, builtin::sulfuric_acid_aq, amount_from_mol(0.025, ok));
		if (base > 0.0) {
			b.set_amount(beaker, builtin::sodium_hydroxide_aq, amount_from_mol(base, ok));
		}
		b.set_temperature(beaker, 293.15);
		std::string why;
		auto world = b.build(&why);
		if (!world) {
			std::fprintf(stderr, "chem_tool: %s\n", why.c_str());
			return 1;
		}
		Simulation sim(std::move(*world), shipped());
		const Totals genesis = totals(table, sim.world().snapshot());
		std::vector<Reaction> equilibria;
		for (const Reaction &r : builtin_reactions()) {
			if (r.kinetics().law == Kinetics::Law::Equilibrium) {
				equilibria.push_back(r);
			}
		}
		const StepTrace trace = sim.step();
		const bool clean = check_all(table, trace, equilibria).empty() && totals(table, sim.world().snapshot()) == genesis;
		const CellState &end = sim.world().snapshot_cell(beaker);
		double kelvin = 0.0, p = 0.0;
		sim.world().temperature(beaker, kelvin);
		if (!ph(table, end, kelvin, p)) {
			std::fprintf(stderr, "chem_tool: no pH\n");
			return 1;
		}
		// the closed form: the heat of what the step made and used (the waters, and the bisulfate's share), at 20 C
		const double closed = 20.0 - heat_of_change(table, trace.before.cells[static_cast<size_t>(beaker.index)], end, 293.15) / thermal_state(table, end).heat_capacity;
		// the ideal: what is left over at unit activity, or water's own ions
		const double kg = water * 0.018015, left = (0.05 - base) / kg;
		const double kw = water_ionisation(table, kelvin);
		const double ideal = left > 0.0 ? -std::log10(0.5 * left + std::sqrt(0.25 * left * left + kw)) : -std::log10(kw / (-0.5 * left + std::sqrt(0.25 * left * left + kw)));
		std::printf("%8d %10.3f %12.3f %12.3f %10.3f %12.5f %10s\n", ml, p, ideal, kelvin - 273.15, closed, static_cast<double>(end.amount[static_cast<size_t>(builtin::hydrogen_sulfate_ion.index)]) * MOL_PER_QUANTUM,
				clean ? "hold" : "BROKEN");
		status |= clean ? 0 : 1;
		status |= std::abs(kelvin - 273.15 - closed) < 0.02 ? 0 : 1;
		// the jump: acid a mL short of the equivalence, near neutral at it, base a mL past (at it, a little over 7: the
		// sulfate made is the bisulfate's base, Stage 63)
		if ((ml == 49 && !(p < 3.0)) || (ml == 50 && !(p > 7.0 && p < 7.6)) || (ml == 51 && !(p > 11.0))) {
			status |= 1;
		}
	}
	std::printf("\n%s\n", status == 0 ? "every mixture settled with its invariants holding, within 0.02 K of the closed form; the pH jumps across 50 mL" : "A MIXTURE FAILED");
	return status;
}

// Precipitation (GAME_SPEC §9, Stage 44): 25 mL of 0.2 M copper sulfate (0.005 mol in 25 g of water), 1 M sodium
// hydroxide added; each mixture settled in one step: the hydroxide made against the closed form (half the base,
// up to the copper), the warmth against the tables' heat, the copper left and the pH. Then the hydroxide held at a
// temperature, its half-life against the first-order closed form.
int precipitation() {
	bool ok = false;
	const SpeciesTable &table = builtin_species();
	const auto hf = [&](SpeciesId x) { return table.species(x).hf; };
	const auto cp = [&](SpeciesId x) { return table.species(x).cp; };
	const double heat = hf(builtin::copper_hydroxide) + hf(builtin::sodium_sulfate_aq) - hf(builtin::copper_sulfate_aq) - 2.0 * hf(builtin::sodium_hydroxide_aq) +
			(cp(builtin::copper_hydroxide) + cp(builtin::sodium_sulfate_aq) - cp(builtin::copper_sulfate_aq) - 2.0 * cp(builtin::sodium_hydroxide_aq)) * (293.15 - T0);
	const double grams = 97.561; // Cu(OH)2, g/mol
	std::printf("25 mL of 0.2 M copper sulfate (0.005 mol in 25 g of water), 1 M sodium hydroxide (0.001 mol in 0.96 g of water a mL) added, mixed at 20 C\n");
	std::printf("CuSO4(aq) + 2 NaOH(aq) -> Cu(OH)2(s) + Na2SO4(aq): %.2f kJ at 20 C (in ions, Stage 63: Cu2+ + 2 OH- -> Cu(OH)2)\n\n", heat * 1e-3);
	std::printf("%8s %12s %12s %14s %8s %10s %10s %10s\n", "base mL", "Cu(OH)2 g", "closed g", "Cu left mol", "pH", "C", "closed C", "invariants");
	std::vector<Reaction> equilibria;
	for (const Reaction &r : builtin_reactions()) {
		if (r.kinetics().law == Kinetics::Law::Equilibrium) {
			equilibria.push_back(r);
		}
	}
	int status = 0;
	for (int ml = 0; ml <= 14; ++ml) {
		const double base = 0.001 * ml, water = (25.0 + 0.96 * ml) / 18.015;
		World::Builder b(table);
		const CellId beaker = b.add_cell("beaker");
		b.set_amount(beaker, builtin::water, amount_from_mol(water, ok)).set_amount(beaker, builtin::copper_sulfate_aq, amount_from_mol(0.005, ok));
		if (base > 0.0) {
			b.set_amount(beaker, builtin::sodium_hydroxide_aq, amount_from_mol(base, ok));
		}
		b.set_temperature(beaker, 293.15);
		std::string why;
		auto world = b.build(&why);
		if (!world) {
			std::fprintf(stderr, "chem_tool: %s\n", why.c_str());
			return 1;
		}
		Simulation sim(std::move(*world), shipped());
		const Totals genesis = totals(table, sim.world().snapshot());
		const StepTrace trace = sim.step();
		const Violations broken = check_all(table, trace, equilibria);
		for (const std::string &v : broken) {
			std::fprintf(stderr, "chem_tool: %d mL: %s\n", ml, v.c_str());
		}
		const bool clean = broken.empty() && totals(table, sim.world().snapshot()) == genesis;
		const CellState &end = sim.world().snapshot_cell(beaker);
		double kelvin = 0.0, p = 0.0;
		sim.world().temperature(beaker, kelvin);
		if (!ph(table, end, kelvin, p)) {
			std::fprintf(stderr, "chem_tool: no pH\n");
			return 1;
		}
		const auto mol = [&](SpeciesId x) { return static_cast<double>(end.amount[static_cast<size_t>(x.index)]) * MOL_PER_QUANTUM; };
		const double made = std::min(0.005, 0.5 * base), hydroxide = mol(builtin::copper_hydroxide);
		const double copper = mol(builtin::copper_ion) + mol(builtin::copper_hydroxide_ion); // what is dissolved of it
		const double closed = 20.0 - heat_of_change(table, trace.before.cells[static_cast<size_t>(beaker.index)], end, 293.15) / thermal_state(table, end).heat_capacity;
		std::printf("%8d %12.5f %12.5f %14.4g %8.3f %10.3f %10.3f %10s\n", ml, hydroxide * grams, made * grams, copper, p, kelvin - 273.15, closed,
				clean ? "hold" : "BROKEN");
		status |= clean ? 0 : 1;
		// the base all taken, by the balance of protons: the solid, copper's hydroxo ion and what is free, less the
		// hydrogen ions freed (Stage 63)
		const double bound = 2.0 * hydroxide + mol(builtin::copper_hydroxide_ion) + mol(builtin::hydroxide_ion) - mol(builtin::hydrogen_ion) - mol(builtin::hydrogen_sulfate_ion);
		status |= std::abs(bound - base) < 1e-9 ? 0 : 1;
		// the hydroxide the base's half up to the copper, within what copper's hydroxo ion holds of it where copper is left
		status |= std::abs(hydroxide - made) < 0.02 * made + 2e-6 ? 0 : 1;
		status |= std::abs(kelvin - 273.15 - closed) < 0.02 ? 0 : 1;
		if (ml >= 11 && !(copper < 1e-11 && p > 12.0)) {
			status |= 1; // past the equivalence, no copper is left in the solution (the last quantum, the equilibrium's own)
		}
	}
	// the hydroxide held at a temperature in dry nitrogen, through a furnace's wall
	std::printf("\nCu(OH)2(s) -> CuO(s) + H2O(g), first order (Fukuda et al. 2019): held in dry nitrogen\n%8s %14s %14s\n", "C", "half-life s", "closed s");
	const Kinetics &k = builtin_reactions()[static_cast<size_t>(builtin::reaction::hydroxide_dehydration)].kinetics();
	for (const double celsius : { 100.0, 125.0, 150.0 }) {
		const double kelvin = celsius + 273.15, closed = std::log(2.0) / (k.prefactor * std::exp(-k.activation / (GAS_CONSTANT * kelvin)));
		World::Builder b(table);
		const CellId cell = b.add_cell("pan");
		const CellId bath = b.add_reservoir("furnace", kelvin);
		b.add_edge(cell, bath, PHASES_NONE, 1e6);
		b.set_amount(cell, builtin::copper_hydroxide, amount_from_mol(1e-6, ok)).set_amount(cell, builtin::glass, amount_from_mol(100.0, ok));
		b.set_amount(cell, builtin::nitrogen, amount_from_mol(1.0, ok)).set_temperature(cell, kelvin);
		std::string why;
		auto world = b.build(&why);
		if (!world) {
			std::fprintf(stderr, "chem_tool: %s\n", why.c_str());
			return 1;
		}
		Simulation sim(std::move(*world), shipped());
		sim.set_step_seconds(std::min(1.0, closed / 200.0));
		const double n0 = sim.world().amount(cell, builtin::copper_hydroxide).mol();
		double t = 0.0;
		while (sim.world().amount(cell, builtin::copper_hydroxide).mol() > 0.5 * n0 && t < 20.0 * closed) {
			sim.step();
			t += sim.step_seconds();
		}
		std::printf("%8.0f %14.1f %14.1f\n", celsius, t, closed);
		status |= std::abs(t / closed - 1.0) < 0.02 ? 0 : 1;
	}
	std::printf("\n%s\n", status == 0 ? "every mixture settled with its invariants holding, the hydroxide and the warmth at the closed forms; the half-lives at theirs" : "A MIXTURE FAILED");
	return status;
}

} // namespace

// Stage 66 (curriculum 35): a seed in a supersaturated solution. 100 g of water held at 25 C with copper sulfate 5 %
// over its saturation, clear; a 10 mg crystal of the pentahydrate dropped in, the beaker still or stirred (0.01 W,
// the lab's stirrer). What comes out of the solution, in how many crystals, and how far from saturation it is left.
int seed() {
	bool ok = false;
	const SpeciesTable &table = builtin_species();
	const Reaction &grow = builtin_reactions()[static_cast<size_t>(builtin::reaction::pentahydrate_crystallization)];
	const double kelvin = 298.15, water_g = 100.0, over = 1.05, seed_mg = 10.0;
	// the model's own solubility: the molality at which the crystals neither grow nor dissolve
	const auto affinity_at = [&](double m, double at = 298.15) {
		CellState state;
		state.amount.assign(static_cast<size_t>(table.species_count()), 0);
		state.particles.assign(static_cast<size_t>(table.species_count()), 0);
		state.amount[static_cast<size_t>(builtin::water.index)] = amount_from_mol(1000.0 / 18.015, ok).quanta;
		put_amount(table, state, builtin::copper_sulfate_aq, amount_from_mol(m, ok).quanta);
		bool defined = false;
		return affinity(table, state, grow.terms(), at, defined);
	};
	const auto solubility_at = [&](double at) {
		double low = 0.5, high = 3.0;
		for (int i = 0; i < 60; ++i) {
			const double mid = 0.5 * (low + high);
			(affinity_at(mid, at) > 0.0 ? high : low) = mid;
		}
		return 0.5 * (low + high);
	};
	for (const double c : { 15.0, 20.0, 25.0, 30.0 }) {
		std::printf("the model's solubility at %.0f C: %.4f mol/kg\n", c, solubility_at(c + 273.15));
	}
	double lo = 0.5, hi = 3.0;
	for (int i = 0; i < 60; ++i) {
		const double mid = 0.5 * (lo + hi);
		(affinity_at(mid) > 0.0 ? hi : lo) = mid;
	}
	const double saturated = 0.5 * (lo + hi);
	std::printf("the model's solubility at 25 C: %.4f mol/kg (%.2f g of CuSO4 in 100 g of water); the solution at %.2f times it, S = %.4f\n", saturated,
			saturated * 159.609 / 10.0, over, std::exp(affinity_at(saturated * over) / (GAS_CONSTANT * kelvin)));
	for (const double watts : { 0.0, 0.01 }) {
		World::Builder b(table);
		const CellId beaker = b.add_rigid_cell("beaker", 250e-6);
		const CellId room = b.add_reservoir("room", kelvin);
		b.add_edge(beaker, room, PHASES_NONE, 50.0); // held at the room's temperature
		const double water = water_g / 18.015;
		b.set_amount(beaker, builtin::water, amount_from_mol(water, ok)).set_amount(beaker, builtin::copper_sulfate_aq, amount_from_mol(saturated * over * water_g * 1e-3, ok));
		b.set_amount(beaker, builtin::copper_sulfate_pentahydrate, amount_from_mol(seed_mg * 1e-3 / 249.677, ok)).set_particles(beaker, builtin::copper_sulfate_pentahydrate, 1);
		b.set_amount(beaker, builtin::glass, amount_from_mol(80.0 / 60.083, ok));
		const double n = P_ATMOSPHERE * (250e-6 - 120e-6) / (GAS_CONSTANT * kelvin);
		b.set_amount(beaker, builtin::nitrogen, amount_from_mol(0.79 * n, ok)).set_amount(beaker, builtin::oxygen, amount_from_mol(0.21 * n, ok));
		b.set_temperature(beaker, kelvin);
		std::string why;
		auto world = b.build(&why);
		if (!world) {
			std::fprintf(stderr, "chem_tool: %s\n", why.c_str());
			return 1;
		}
		Simulation sim(std::move(*world), shipped());
		sim.set_checking(false);
		sim.world().set_stirring(beaker, watts);
		const double excess_g = (saturated * (over - 1.0)) * water_g * 1e-3 * 249.677; // about: the excess as pentahydrate, its water taken from the solution aside
		std::printf("\n%s (%.2f W): a %.0f mg seed; the excess about %.3f g of the pentahydrate\n", watts > 0.0 ? "stirred" : "still", watts, seed_mg, excess_g);
		std::printf("%8s %12s %12s %10s %8s\n", "time min", "crystals g", "crystals", "size um", "S");
		for (int step = 0; step <= 60 * 7200; ++step) {
			if (step % (60 * 600) == 0) {
				const CellState &c = sim.world().snapshot_cell(beaker);
				bool defined = false;
				double t = 0.0;
				sim.world().temperature(beaker, t);
				const double a = affinity(table, c, grow.terms(), t, defined);
				std::printf("%8.0f %12.5f %12lld %10.1f %8.4f\n", step * STEP_SECONDS / 60.0, sim.world().amount(beaker, builtin::copper_sulfate_pentahydrate).mol() * 249.677,
						static_cast<long long>(sim.world().particles(beaker, builtin::copper_sulfate_pentahydrate)),
						particle_diameter(table, c, builtin::copper_sulfate_pentahydrate) * 1e6, std::exp(a / (GAS_CONSTANT * t)));
			}
			sim.step();
		}
	}
	return 0;
}

int spectrum_study(); // spectrum.cpp
int frames_study(); // frames.cpp
int tilt_study(); // lying.cpp
int speed_study(); // speed.cpp
int vortex_study();

int run_study(const char *name) {
	if (std::strcmp(name, "frames") == 0) {
		return frames_study();
	}
	if (std::strcmp(name, "spectrum") == 0) {
		return spectrum_study();
	}
	if (std::strcmp(name, "speed") == 0) {
		return speed_study();
	}
	if (std::strcmp(name, "tilt") == 0) {
		return tilt_study();
	}
	if (std::strcmp(name, "vortex") == 0) {
		return vortex_study();
	}
	if (std::strcmp(name, "cooling") == 0) {
		return cooling();
	}
	if (std::strcmp(name, "decomposition") == 0) {
		return decomposition();
	}
	if (std::strcmp(name, "balloon") == 0) {
		return balloon();
	}
	if (std::strcmp(name, "distillation") == 0) {
		return distillation();
	}
	if (std::strcmp(name, "dissolution") == 0) {
		return dissolution();
	}
	if (std::strcmp(name, "crystals") == 0) {
		return crystals();
	}
	if (std::strcmp(name, "solution_heat") == 0) {
		return solution_heat();
	}
	if (std::strcmp(name, "neutralisation") == 0) {
		return neutralisation();
	}
	if (std::strcmp(name, "precipitation") == 0) {
		return precipitation();
	}
	if (std::strcmp(name, "seed") == 0) {
		return seed();
	}
	std::fprintf(stderr, "chem_tool study: unknown study \"%s\" (cooling, decomposition, balloon, distillation, dissolution, crystals, solution_heat, neutralisation, precipitation, seed, spectrum, frames, tilt, vortex)\n", name);
	return 2;
}
