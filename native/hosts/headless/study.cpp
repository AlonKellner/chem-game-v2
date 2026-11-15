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

} // namespace

int run_study(const char *name) {
	if (std::strcmp(name, "cooling") == 0) {
		return cooling();
	}
	std::fprintf(stderr, "chem_tool study: unknown study \"%s\" (cooling)\n", name);
	return 2;
}
