// Measured kinetics (GAME_SPEC §7, Stage 23): each rate against its source, at the source's own
// conditions; the transport step from measured diffusion and water's viscosity.
#include "chem/builtin_ids.hpp"
#include "tables.hpp"
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

const Reaction &reaction(int index) {
	return builtin_reactions()[static_cast<size_t>(index)];
}

// a kilogram of water and what is put in it
struct Cell {
	CellState state{ std::vector<int64_t>(static_cast<size_t>(builtin_species().species_count()), 0), 0,
		std::vector<int64_t>(static_cast<size_t>(builtin_species().species_count()), 0) };
	Cell() { put(builtin::water, 1.0 / 0.018015); }
	Cell &put(SpeciesId s, double mol, int64_t particles = 0) {
		state.amount[static_cast<size_t>(s.index)] = quanta(mol);
		state.particles[static_cast<size_t>(s.index)] = particles;
		return *this;
	}
};

// a within rel of b, as a share of b (doctest's Approx has a scale of 1: useless for rates of 1e-5)
bool near(double a, double b, double rel) {
	return b != 0.0 && std::abs(a / b - 1.0) < rel;
}

double affinity_of(const Cell &c, const Reaction &r, double kelvin) {
	bool defined = false;
	const double a = affinity(builtin_species(), c.state, r.terms(), kelvin, defined);
	REQUIRE(defined);
	return a;
}

// the model's saturation of the pentahydrate in pure water, mol/kg
double saturation(double kelvin) {
	const Reaction &r = reaction(builtin::reaction::pentahydrate_crystallization);
	double lo = 0.05, hi = 8.0;
	for (int i = 0; i < 70; ++i) {
		const double middle = 0.5 * (lo + hi);
		(affinity_of(Cell().put(builtin::copper_sulfate_aq, middle), r, kelvin) > 0.0 ? hi : lo) = middle;
	}
	return 0.5 * (lo + hi);
}

// the volume of a kilogram of water, m^3, as the model has it
double litres_of_water() {
	return 1.0 / 0.018015 * builtin_species().species(builtin::water).molar_volume;
}

} // namespace

TEST_CASE("measured: water's viscosity, and the ions' diffusion as they move together (Stage 23)") {
	const SpeciesTable &t = builtin_species();
	// the table's own points (IAPWS 2008, NIST Chemistry WebBook at 0.5 MPa), and between them in the logarithm
	CHECK(near(solvent_viscosity(t, 298.15), 0.88997e-3, 1e-9));
	CHECK(near(solvent_viscosity(t, 373.15), 0.28169e-3, 1e-9));
	CHECK(near(solvent_viscosity(t, 308.15), std::sqrt(0.79721e-3 * 0.65278e-3), 1e-9));
	CHECK(near(solvent_viscosity(t, 318.15), 0.59584e-3, 0.005)); // 45 C, from the WebBook apart
	CHECK(solvent_viscosity(t, 250.0) == solvent_viscosity(t, 270.0)); // held beyond the ends
	CHECK(solvent_viscosity(t, 600.0) == solvent_viscosity(t, 423.15));
	// Nernst-Hartley: (|z+| + |z-|) D+ D- / (|z+| D+ + |z-| D-), Li and Gregory's ions at 25 C
	const double cu = 7.33e-10, so4 = 1.07e-9, h = 9.31e-9;
	CHECK(near(diffusion(t, builtin::copper_sulfate_aq, 298.15), 4.0 * cu * so4 / (2.0 * cu + 2.0 * so4), 1e-12));
	CHECK(near(diffusion(t, builtin::sulfuric_acid_aq, 298.15), 3.0 * h * so4 / (1.0 * h + 2.0 * so4), 1e-12));
	// Stokes-Einstein: as T / viscosity
	CHECK(diffusion(t, builtin::copper_sulfate_aq, 333.15) / diffusion(t, builtin::copper_sulfate_aq, 298.15) ==
			doctest::Approx(333.15 / 298.15 * 0.88997 / 0.46613).epsilon(1e-9));
	// what is not dissolved, or whose particles have no coefficient, has none: transport puts no limit on it
	CHECK(diffusion(t, builtin::water, 298.15) == 0.0);
	const SpeciesTable &neutral = testing::with_neutral_solute();
	CHECK(diffusion(neutral, neutral.find("urea_aq"), 298.15) == 0.0);
}

TEST_CASE("measured: copper oxide dissolves at Majima's rate, half order in the acid, controlled at its surface (Stage 23)") {
	// 2.5e-7 mol/cm2/min in 0.2 M sulfuric acid at 25 C (Nicol and Akilan 2018, Table 4, citing Majima et al.
	// 1980), half order in the acid; the rate does not depend on agitation
	const Reaction &r = reaction(builtin::reaction::copper_oxide_dissolution);
	const auto steps_at = [&](double acid, double mixing) {
		Cell c;
		c.put(builtin::sulfuric_acid_aq, acid).put(builtin::copper_oxide, 0.01, 240000000); // grains of 10 um
		return surface_steps(builtin_species(), c.state, r, 298.15, affinity_of(c, r, 298.15), mixing);
	};
	const SurfaceSteps still = steps_at(0.2, 1.0);
	CHECK(near(still.surface, 2.5e-7 * 1e4 / 60.0, 0.002));
	CHECK(near(steps_at(0.8, 1.0).surface / still.surface, 2.0, 0.002));
	REQUIRE(still.transported);
	CHECK(still.transport > 100.0 * still.surface); // the acid reaches the grains far faster than they use it
	CHECK(steps_at(0.2, 3.0).both / still.both < 1.01); // so stirring hardly matters
	CHECK(near(still.both, 1.0 / (1.0 / still.surface + 1.0 / still.transport), 1e-12));
}

TEST_CASE("measured: the pentahydrate grows at Tanimoto's surface rate, in series with transport to it (Stage 23)") {
	// Tanimoto, Kobayashi and Fujita 1963, Table 1: the surface step N = kr (C - C_s), kr in m/h, in series with
	// mass transfer (1/K = 1/kd + 1/kr); seeds of -28+32 mesh, about half a millimetre
	const struct {
		double celsius, kr;
	} rows[] = { { 20.0, 0.030 }, { 30.0, 0.063 }, { 40.0, 0.120 } };
	const Reaction &r = reaction(builtin::reaction::pentahydrate_crystallization);
	for (const auto &row : rows) {
		CAPTURE(row.celsius);
		const double kelvin = row.celsius + 273.15, ms = saturation(kelvin), m = 1.01 * ms;
		Cell c;
		c.put(builtin::copper_sulfate_aq, m).put(builtin::copper_sulfate_pentahydrate, 0.001, 1400); // 1400 crystals of 0.53 mm
		const SurfaceSteps still = surface_steps(builtin_species(), c.state, r, kelvin, affinity_of(c, r, kelvin), 1.0);
		const double gap = (m - ms) / litres_of_water(); // mol/m^3
		CHECK(near(still.surface, row.kr / 3600.0 * gap, 0.03));
		// transport: Sh = 2 at rest, the measured diffusion, the gap to the model's own saturation
		const double d = particle_diameter(builtin_species(), c.state, builtin::copper_sulfate_pentahydrate);
		CHECK(near(d, 0.53e-3, 0.02));
		REQUIRE(still.transported);
		CHECK(near(still.transport, 2.0 * diffusion(builtin_species(), builtin::copper_sulfate_aq, kelvin) / d * gap, 0.02));
		CHECK(near(still.both, 1.0 / (1.0 / still.surface + 1.0 / still.transport), 1e-12));
		const SurfaceSteps stirred = surface_steps(builtin_species(), c.state, r, kelvin, affinity_of(c, r, kelvin), 3.0);
		CHECK(near(stirred.transport, 3.0 * still.transport, 1e-12));
		CHECK(stirred.surface == still.surface); // stirring brings the salt, and does not change the surface's own step
		CHECK(stirred.both > still.both);
		MESSAGE(row.celsius << " C: surface " << still.surface << ", transport " << still.transport << " mol/m2/s; stirred " << stirred.both / still.both << " times faster");
	}
}

TEST_CASE("measured: a crystal in water dissolves by transport, read from one quantum when none is dissolved yet (Stage 23)") {
	const Reaction &r = reaction(builtin::reaction::pentahydrate_crystallization);
	Cell c;
	c.put(builtin::copper_sulfate_pentahydrate, 0.001, 1400);
	const double a = affinity_of(c, r, 298.15); // -infinity: none of the salt is dissolved
	CHECK(a < 0.0);
	const SurfaceSteps steps = surface_steps(builtin_species(), c.state, r, 298.15, a, 1.0);
	REQUIRE(steps.transported);
	// the gap is the whole saturation: transport carries it away as from a solution with none
	const double d = particle_diameter(builtin_species(), c.state, builtin::copper_sulfate_pentahydrate);
	const double saturated = saturation(298.15) / litres_of_water();
	CHECK(near(steps.transport, 2.0 * diffusion(builtin_species(), builtin::copper_sulfate_aq, 298.15) / d * saturated, 0.01));
	CHECK(steps.both > 0.0);
	CHECK(steps.both < steps.surface);
	MESSAGE("dissolving into pure water: transport " << steps.transport << " mol/m2/s, against " << 2.0 * diffusion(builtin_species(), builtin::copper_sulfate_aq, 298.15) / d * saturated << " from the saturation");
}

TEST_CASE("measured: malachite at Koga's isothermal run: nearly all gone after 589 minutes at 498 K (Stage 23)") {
	// Aoki, Yamamoto and Koga 2021, ESI: the second (main) step, Ea 147.6 kJ/mol, A 6.05e11 /s; at 498 K,
	// alpha = 0.99 after 589 min
	World::Builder b(builtin_species());
	const CellId cell = b.add_cell("pan");
	const CellId bath = b.add_reservoir("furnace", 498.0);
	b.add_edge(cell, bath, PHASES_NONE, 1e6);
	b.set_amount(cell, builtin::malachite, Amount{ quanta(1e-6) }).set_amount(cell, builtin::glass, Amount{ quanta(100.0) });
	b.set_amount(cell, builtin::nitrogen, Amount{ quanta(1.0) }).set_temperature(cell, 498.0);
	std::string why;
	auto world = b.build(&why);
	REQUIRE_MESSAGE(world.has_value(), why);
	Simulation sim(std::move(*world), std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end()));
	sim.set_step_seconds(1.0);
	const double n0 = sim.world().amount(cell, builtin::malachite).mol();
	for (int s = 0; s < 589 * 60; ++s) {
		sim.step();
	}
	const double alpha = 1.0 - sim.world().amount(cell, builtin::malachite).mol() / n0;
	CHECK(alpha >= 0.99);
	CHECK(near(alpha, 1.0 - std::exp(-6.05e11 * std::exp(-147600.0 / (GAS_CONSTANT * 498.0)) * 589.0 * 60.0), 1e-4));
	MESSAGE("alpha after 589 min at 498 K: " << alpha);
}

TEST_CASE("measured: each dehydration's rate at its DTG peak is the first-order peak condition at 10 C/min (Stage 23)") {
	// Yu 2007, Table 2: DTG peaks at 363.14, 393.39 and 529.08 K at 10 C/min; at a first-order peak,
	// k(Tp) = beta Ea / (R Tp^2) (Kissinger). Ea: Cheng et al. 2019, whose own fits print log10 A of 8.40,
	// 13.23 and 15.09: within a factor of 1.3, 1.5 and 5.1 of these.
	const struct {
		int reaction;
		double peak, cheng_log_a;
	} rows[] = { { builtin::reaction::pentahydrate_dehydration, 363.14, 8.40 }, { builtin::reaction::trihydrate_dehydration, 393.39, 13.23 },
		{ builtin::reaction::monohydrate_dehydration, 529.08, 15.09 } };
	for (const auto &row : rows) {
		const Kinetics &k = reaction(row.reaction).kinetics();
		CAPTURE(row.peak);
		const double rate = k.prefactor * std::exp(-k.activation / (GAS_CONSTANT * row.peak));
		CHECK(near(rate, 10.0 / 60.0 * k.activation / (GAS_CONSTANT * row.peak * row.peak), 1e-3));
		CHECK(std::abs(std::log10(k.prefactor) - row.cheng_log_a) < std::log10(5.2));
	}
}
