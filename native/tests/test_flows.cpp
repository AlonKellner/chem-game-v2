// The last step's flows (GAME_SPEC §13.6): what the step's primitives moved,
// summed by process; the ledger's sums, kept whether the step was traced or not.
#include "chem/builtin_ids.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"

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

struct Rig {
	CellId flask, balloon;
	EdgeId neck, burner, loss;
	Simulation sim;
};

// malachite in a flask over a flame, its gas into a balloon
Rig heated() {
	World::Builder b(builtin_species());
	const CellId flask = b.add_rigid_cell("flask", FLASK);
	const CellId balloon = b.add_cell("balloon");
	const CellId flame = b.add_reservoir("flame", 2200.0);
	const CellId room = b.add_reservoir("room", 293.15);
	const EdgeId neck = b.add_edge(flask, balloon, PHASES_GAS);
	const EdgeId burner = b.add_edge(flask, flame, PHASES_NONE, 0.2);
	const EdgeId loss = b.add_edge(flask, room, PHASES_NONE, 0.9);
	b.add_edge(balloon, room, PHASES_NONE, 0.5);
	const double n = 5.0 / 221.114;
	b.set_amount(flask, builtin::malachite, mol(n)).set_amount(flask, builtin::glass, mol(100.0 / 60.083));
	const double air = P_ATMOSPHERE * (FLASK - n * 221.114 / 3.8 * 1e-6) / (GAS_CONSTANT * 293.15);
	b.set_amount(flask, builtin::nitrogen, mol(0.79 * air)).set_amount(flask, builtin::oxygen, mol(0.21 * air));
	b.set_temperature(flask, 293.15);
	b.set_amount(balloon, builtin::latex, mol(2.0 / 68.119)).set_temperature(balloon, 293.15);
	std::string why;
	auto world = b.build(&why);
	REQUIRE_MESSAGE(world.has_value(), why);
	return Rig{ flask, balloon, neck, burner, loss, Simulation(std::move(*world), std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end())) };
}

} // namespace

TEST_CASE("flows: the last step's flows are the ledger's sums by process, heat from the hotter cell to the colder (Stage 18)") {
	Rig rig = heated();
	Simulation &sim = rig.sim;
	sim.set_checking(true); // every invariant, T1 among them, on every step
	const SpeciesTable &table = builtin_species();
	const StepFlows &f = sim.flows();
	bool decomposed = false, pushed = false;
	for (int step = 0; step < 60 * 400; ++step) {
		const StepTrace trace = sim.step();
		REQUIRE(f.reactions == sim.rated().size() + sim.equilibria().size());
		REQUIRE(f.energy.size() == trace.edges.size() * PROCESSES);
		// the ledger, summed by edge and by cell and reaction
		std::vector<int64_t> energy(trace.edges.size(), 0), extent(f.extent.size(), 0);
		std::vector<double> grams(trace.edges.size(), 0.0);
		for (const LedgerEntry &e : trace.ledger) {
			if (e.kind == LedgerEntry::Kind::Transfer) {
				const auto i = static_cast<size_t>(e.edge.index);
				const int64_t sign = e.from == trace.edges[i].a ? 1 : -1;
				energy[i] += sign * e.energy.quanta;
				for (const SpeciesAmount &x : e.delta) {
					grams[i] += static_cast<double>(sign) * x.amount.mol() * static_cast<double>(table.molar_mass_ug(x.species)) * 1e-6;
				}
			}
		}
		for (size_t i = 0; i < trace.edges.size(); ++i) {
			int64_t e = 0;
			double g = 0.0;
			for (size_t p = 0; p < PROCESSES; ++p) {
				e += f.energy[i * PROCESSES + p];
				g += f.grams[i * PROCESSES + p];
			}
			REQUIRE(e == energy[i]);
			REQUIRE(g == doctest::Approx(grams[i]).epsilon(1e-12));
			// heat conduction carries no matter, and runs from the hotter end to the colder
			REQUIRE(f.grams[i * PROCESSES + static_cast<size_t>(Process::Heat)] == 0.0);
			const int64_t heat = f.energy[i * PROCESSES + static_cast<size_t>(Process::Heat)];
			if (heat != 0) {
				const auto kelvin = [&](CellId c) {
					const CellMeta &m = trace.meta[static_cast<size_t>(c.index)];
					return m.reservoir ? m.reservoir_temperature : thermal_state(table, trace.before.cells[static_cast<size_t>(c.index)]).temperature;
				};
				const double gap = kelvin(trace.edges[i].a) - kelvin(trace.edges[i].b);
				if (std::fabs(gap) > 1.0) { // heat flows after the reactions, edge by edge: T1 (checked every step here) is the exact rule
					REQUIRE((heat > 0) == (gap > 0.0));
				}
			}
		}
		const int64_t burned = f.extent[static_cast<size_t>(rig.flask.index) * f.reactions + 0]; // malachite's decomposition, the first rated
		decomposed = decomposed || burned > 0;
		pushed = pushed || f.grams[static_cast<size_t>(rig.neck.index) * PROCESSES + static_cast<size_t>(Process::Settle)] > 0.0;
	}
	CHECK(decomposed);
	CHECK(pushed); // the gas made pushed into the balloon, as the settling's flow
	CHECK(f.energy[static_cast<size_t>(rig.burner.index) * PROCESSES + static_cast<size_t>(Process::Heat)] < 0); // into the flask, from the flame (b)
}

TEST_CASE("flows: kept the same whether the step was traced or not (Stage 18)") {
	Rig traced = heated(), untraced = heated();
	for (int step = 0; step < 60 * 200; ++step) {
		traced.sim.step();
		untraced.sim.advance();
		if (step % 997 == 0 || step == 60 * 200 - 1) {
			REQUIRE(traced.sim.flows().energy == untraced.sim.flows().energy);
			REQUIRE(traced.sim.flows().grams == untraced.sim.flows().grams);
			REQUIRE(traced.sim.flows().extent == untraced.sim.flows().extent);
		}
	}
}
