// Metals (GAME_SPEC §7, §9, §12, Stage 92): the five metals and hydrogen, their rows and colours held to their sources;
// displacement and magnesium in acid at the transport limit their sources measured; iron and zinc in acid at their
// measured corrosion rates; a solid a surface reaction makes comes as its own grains (docs/DATA_SOURCES.md, Metals and
// displacement (Stage 92)).
#include "chem/builtin_ids.hpp"
#include "tables.hpp"
#include "chem/invariants.hpp"
#include "chem/optics.hpp"
#include "chem/sim.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

using namespace chem;

namespace {

int64_t quanta(double n) {
	bool ok = false;
	const Amount a = amount_from_mol(n, ok);
	REQUIRE(ok);
	return a.quanta;
}

const Reaction &shipped(int index) {
	return builtin_reactions()[static_cast<size_t>(index)];
}

// a kilogram of water and what is put in it
struct Cell {
	CellState state{ std::vector<int64_t>(static_cast<size_t>(builtin_species().species_count()), 0), 0,
		std::vector<int64_t>(static_cast<size_t>(builtin_species().species_count()), 0) };
	Cell() { put(builtin::water, 1.0 / 0.018015); }
	Cell &put(SpeciesId s, double mol, int64_t particles = 0) {
		testing::put_amount(builtin_species(), state, s, quanta(mol));
		state.particles[static_cast<size_t>(s.index)] = particles;
		return *this;
	}
};

bool near(double a, double b, double rel) {
	return b != 0.0 && std::abs(a / b - 1.0) < rel;
}

// a reaction's rate (both steps in series) in a cell at `kelvin`, stirred at `dissipation` W/kg
SurfaceSteps steps_of(const Cell &c, const Reaction &r, double kelvin, double dissipation) {
	const CellState settled = testing::settled(c.state, kelvin);
	bool defined = false;
	const double a = affinity(builtin_species(), settled, r.terms(), kelvin, defined);
	REQUIRE(defined);
	return surface_steps(builtin_species(), settled, r, kelvin, a, dissipation);
}

// an activation energy from two rates, J/mol
double activation(double cold, double warm, double t_cold, double t_warm) {
	return GAS_CONSTANT * std::log(warm / cold) / (1.0 / t_cold - 1.0 / t_warm);
}

// a metal's n + k table (data/optics/metal_*.csv): nm, n, k
std::vector<std::array<double, 3>> nk_of(const std::string &metal) {
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/data/optics/metal_" + metal + ".csv");
	REQUIRE(in);
	std::vector<std::array<double, 3>> rows;
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') {
			continue;
		}
		std::array<double, 3> r{};
		char comma = 0;
		std::istringstream(line) >> r[0] >> comma >> r[1] >> comma >> r[2];
		rows.push_back(r);
	}
	return rows;
}

double fresnel(double n, double k) {
	return ((n - 1.0) * (n - 1.0) + k * k) / ((n + 1.0) * (n + 1.0) + k * k);
}

} // namespace

TEST_CASE("measured: the metals' and hydrogen's rows are NBS's, their densities NIST's, hydrogen's diffusion in air Marrero and Mason's (Stage 92)") {
	using namespace builtin;
	const SpeciesTable &t = builtin_species();
	// NBS 1982 (Wagman et al.): S and Cp of the elements as crystals (pp. 2-154, 2-138, 2-260, 2-177, 2-160), their
	// formation nought; densities (g/cm3) NIST's (Hubbell and Seltzer, X-ray mass attenuation, Table 1)
	const struct {
		SpeciesId s;
		double s0, cp, rho, molar_mass;
	} rows[] = { { copper, 33.150, 24.435, 8.960, 63.546 }, { zinc, 41.63, 25.40, 7.133, 65.38 }, { magnesium, 32.68, 24.89, 1.740, 24.305 },
		{ iron, 27.28, 25.10, 7.874, 55.845 }, { silver, 42.55, 25.351, 10.50, 107.87 } };
	for (const auto &r : rows) {
		const SpeciesDef &d = t.species(r.s);
		CHECK(d.phase == Phase::Solid);
		CHECK(d.hf == 0.0);
		CHECK(d.s0 == doctest::Approx(r.s0).epsilon(1e-12));
		CHECK(d.cp == doctest::Approx(r.cp).epsilon(1e-12));
		CHECK(d.molar_volume == doctest::Approx(r.molar_mass / r.rho * 1e-6).epsilon(1e-3));
		CHECK(d.grain == doctest::Approx(20e-6).epsilon(1e-12)); // (design: what comes out of a solution)
	}
	// p. 2-38: H2(g) S 130.684, Cp 28.824; in air 0.779 cm2/s at 298.15 K as T^1.75 (Marrero and Mason 1972, Figure 39:
	// pD = 3.64e-5 T^1.750 atm cm2/s)
	const SpeciesDef &h2 = t.species(hydrogen);
	CHECK(h2.phase == Phase::Gas);
	CHECK(h2.s0 == doctest::Approx(130.684).epsilon(1e-12));
	CHECK(h2.cp == doctest::Approx(28.824).epsilon(1e-12));
	CHECK(h2.air_diffusion == doctest::Approx(3.64e-5 * std::pow(298.15, 1.75) * 1e-4).epsilon(0.002));
	// copper nitrate: its ions' sum
	const SpeciesDef &nitrate = t.species(copper_nitrate_aq);
	CHECK(nitrate.hf == doctest::Approx(t.species(copper_ion).hf + 2.0 * t.species(nitrate_ion).hf).epsilon(1e-9));
}

TEST_CASE("measured: each metal reflects its measured n + k by Fresnel, copper red, silver bright, iron grey; a deposit darker; metal dark in the long wave (Stage 92)") {
	using namespace builtin;
	const auto at = [](SpeciesId s, int nm) { return optics::spectrum(s)[static_cast<size_t>(nm - 380)]; };
	// at a measured point the reflectance is Fresnel's of its n + k exactly (McPeak et al. 2015's first point, 380 nm)
	for (const auto &[s, name] : { std::pair{ copper, "copper" }, std::pair{ silver, "silver" } }) {
		const auto rows = nk_of(name);
		REQUIRE(!rows.empty());
		REQUIRE(rows.front()[0] == 380.0);
		CHECK(at(s, 380) == doctest::Approx(fresnel(rows.front()[1], rows.front()[2])).epsilon(1e-9));
	}
	CHECK(at(copper, 650) > at(copper, 450) + 0.3); // its red edge near 580 nm
	CHECK(at(silver, 450) > 0.95);
	CHECK(std::abs(at(iron, 650) - at(iron, 450)) < 0.05); // grey
	CHECK(at(iron, 550) == doctest::Approx(0.51).epsilon(0.03));
	// fine grains darker (design, as cemented copper looks), a piece as its metal
	const optics::Fine *fine = optics::fine(copper);
	REQUIRE(fine != nullptr);
	CHECK(fine->below_um == 100.0);
	CHECK(fine->factor < 1.0);
	// long wave: 1 - R at 10 um (Ordal et al. 1985's copper: 0.0082)
	CHECK(optics::lwir_emissivity("copper").value_or(1.0) == doctest::Approx(0.0082).epsilon(1e-9));
	CHECK(optics::lwir_emissivity("iron").value_or(1.0) == doctest::Approx(0.0335).epsilon(1e-9));
}

TEST_CASE("measured: copper is put out on zinc and iron as fast as transport brings it, at the temperature dependence the cementation studies measured (Stage 92)") {
	using namespace builtin;
	// Sareyed-Dim 1971 (Colorado School of Mines thesis T-1397): Cu2+ on an iron disc, first order, the Levich rate
	// (transport) within 10 %, Ea 14.2 kJ/mol (3.4 kcal); the studies Strickland and Lawson and others collect: 3 to 5
	// kcal/mol (12.6 to 21 kJ/mol), transport-controlled
	for (const int id : { reaction::zinc_displaces_copper, reaction::iron_displaces_copper }) {
		const Reaction &r = shipped(id);
		CAPTURE(id);
		const SpeciesId metal = r.kinetics().in;
		const auto steps = [&](double copper_mol, double kelvin, double dissipation) {
			Cell c;
			c.put(copper_ion, copper_mol).put(sulfate_ion, copper_mol).put(metal, 0.02, 10); // ten pieces of about 2.8 mm
			return steps_of(c, r, kelvin, dissipation);
		};
		const SurfaceSteps cold = steps(7.9e-4, 298.15, 0.5), warm = steps(7.9e-4, 308.15, 0.5); // 50 ppm, stirred
		REQUIRE(cold.transported);
		CHECK(cold.both > 0.999 * cold.transport); // the surface far faster: transport sets the pace
		CHECK(near(steps(2 * 7.9e-4, 298.15, 0.5).both / cold.both, 2.0, 0.02)); // first order in copper
		CHECK(steps(7.9e-4, 298.15, 5.0).both > 1.3 * cold.both); // faster stirred
		const double ea = activation(cold.both, warm.both, 298.15, 308.15);
		MESSAGE("reaction " << id << ": Ea " << ea / 1000.0 << " kJ/mol");
		CHECK(ea > 12.6e3);
		CHECK(ea < 21.0e3);
	}
}

TEST_CASE("measured: magnesium dissolves in hydrochloric acid as fast as transport brings the acid, first order, at Roald's activation energy (Stage 92)") {
	using namespace builtin;
	// Roald 1950 (Lehigh dissertation): first order in HCl to about 0.1 N, the rate with stirring as rpm^0.71, Ea 4100
	// cal/mol (17.2 kJ/mol, Table VII, 25 to 45 C): transport-controlled
	const Reaction &r = shipped(reaction::magnesium_in_acid);
	const auto steps = [&](double acid, double kelvin, double dissipation) {
		Cell c;
		c.put(hydrogen_ion, acid).put(chloride_ion, acid).put(magnesium, 0.002, 4);
		return steps_of(c, r, kelvin, dissipation);
	};
	const SurfaceSteps cold = steps(0.02, 298.15, 0.5), warm = steps(0.02, 318.15, 0.5);
	REQUIRE(cold.transported);
	CHECK(cold.both > 0.999 * cold.transport);
	CHECK(near(steps(0.04, 298.15, 0.5).both / cold.both, 2.0, 0.03));
	const double ea = activation(cold.both, warm.both, 298.15, 318.15);
	MESSAGE("magnesium in acid: Ea " << ea / 1000.0 << " kJ/mol (Roald 17.2)");
	CHECK(near(ea, 17.2e3, 0.25));
}

TEST_CASE("measured: iron and zinc dissolve in 1 M hydrochloric acid at Diki et al.'s and Abdallah et al.'s corrosion rates and activation energies (Stage 92)") {
	using namespace builtin;
	// Diki, Coulibaly, Kassi and Trokourey 2021 (J. Electrochem. Sci. Eng. 11, 97), Table 4: mild steel in 1 M HCl at
	// 298 K, aerated and unstirred, the blank's corrosion current 451.06 uA/cm2: 451.06e-6 / 2F mol of iron a cm2 a
	// second; Table 3: Ea 92.23 kJ/mol (weight loss, 298 to 318 K)
	const Reaction &r = shipped(reaction::iron_in_acid);
	const auto surface = [&](double kelvin) {
		Cell c;
		c.put(hydrogen_ion, 1.0).put(chloride_ion, 1.0).put(iron, 0.01, 10);
		return steps_of(c, r, kelvin, 0.0).surface;
	};
	const double per_m2 = 451.06e-6 / (2.0 * 96485.33) * 1e4;
	MESSAGE("iron in 1 M HCl: " << surface(298.15) << " mol/m2/s (Diki et al. " << per_m2 << ")");
	CHECK(near(surface(298.15), per_m2, 0.01));
	CHECK(near(activation(surface(298.15), surface(318.15), 298.15, 318.15), 92.23e3, 0.01));
	// zinc: Abdallah, Al-Gorair et al. 2026 (BMC Chemistry, doi 10.1186/s13065-026-01948-1), 99.99 % zinc in 1.0 M HCl,
	// Table 3: the blank's weight loss 12.72 mm/year at 294 K (zinc 7.133 g/cm3, 65.38 g/mol); Table 4: Ea 16.484 kJ/mol
	const Reaction &z = shipped(reaction::zinc_in_acid);
	const auto zinc_surface = [&](double kelvin) {
		Cell c;
		c.put(hydrogen_ion, 1.0).put(chloride_ion, 1.0).put(zinc, 0.01, 10);
		return steps_of(c, z, kelvin, 0.0).surface;
	};
	const double zinc_per_m2 = 12.72e-3 * 7133.0 / 0.06538 / (365.25 * 86400.0);
	MESSAGE("zinc in 1 M HCl: " << zinc_surface(294.15) << " mol/m2/s (Abdallah et al. " << zinc_per_m2 << ")");
	CHECK(near(zinc_surface(294.15), zinc_per_m2, 0.01));
	CHECK(near(activation(zinc_surface(294.15), zinc_surface(314.15), 294.15, 314.15), 16.484e3, 0.01));
	// so magnesium, at its transport limit, fizzes far faster than zinc, and zinc faster than iron
	CHECK(zinc_surface(298.15) > surface(298.15));
}

TEST_CASE("metals: none displaces a more reactive one; zinc put in copper sulfate gives copper as fine grains, every invariant held (Stage 92)") {
	using namespace builtin;
	// a kg of water with 0.01 mol of an ion's sulfate, ten pieces of a metal (0.02 mol), at 25 C in a bath, at rest
	const auto run = [](SpeciesId piece, SpeciesId ion, double seconds, const std::function<void(const World &, CellId)> &then) {
		World::Builder b(builtin_species());
		const CellId cell = b.add_cell("beaker");
		const CellId bath = b.add_reservoir("bath", 298.15);
		b.add_edge(cell, bath, PHASES_NONE, 1e6);
		bool ok = false;
		b.set_amount(cell, water, amount_from_mol(55.508, ok));
		b.set_amount(cell, ion, amount_from_mol(0.01, ok)).set_amount(cell, sulfate_ion, amount_from_mol(0.01, ok));
		b.set_amount(cell, piece, amount_from_mol(0.02, ok));
		b.set_particles(cell, piece, 10);
		b.set_temperature(cell, 298.15);
		std::string why;
		auto world = b.build(&why);
		REQUIRE_MESSAGE(world.has_value(), why);
		Simulation sim(std::move(*world), std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end()));
		StepTrace trace;
		for (int step = 1; step <= static_cast<int>(seconds / STEP_SECONDS); ++step) {
			sim.step(trace);
			if (step % 600 == 0) {
				const Violations v = check_all(builtin_species(), trace, sim.equilibria());
				REQUIRE_MESSAGE(v.empty(), v.front());
			}
		}
		then(sim.world(), cell);
	};
	// copper in zinc sulfate: nothing
	run(copper, zinc_ion, 60.0, [](const World &w, CellId cell) { CHECK(w.amount(cell, zinc).quanta == 0); });
	// zinc in copper sulfate: copper put out, mole for mole, as grains of at most its 20 um; the pieces stay ten
	run(zinc, copper_ion, 600.0, [](const World &w, CellId cell) {
		const double made = static_cast<double>(w.amount(cell, copper).quanta) * MOL_PER_QUANTUM;
		const double lost = 0.02 - static_cast<double>(w.amount(cell, zinc).quanta) * MOL_PER_QUANTUM;
		MESSAGE("zinc in copper sulfate, 600 s at rest: " << made << " mol copper");
		CHECK(made > 1e-7);
		CHECK(made == doctest::Approx(lost).epsilon(1e-6));
		CHECK(particle_diameter(builtin_species(), w.snapshot_cell(cell), copper) <= 20e-6 * 1.001);
		CHECK(w.particles(cell, zinc) == 10);
	});
}

TEST_CASE("measured: magnesium's own hydrogen stirs its acid as Roald measured in still acid, the bubbles' power g u (Stage 92)") {
	using namespace builtin;
	// Roald 1950: an 11 mm bar, 10 to 15 cm2 exposed, in 4 L of HCl at 25 C, not rotated: k = 0.20 cm/min (Table V,
	// 0 rpm; the HCl's flux over its concentration), where still liquid would carry it some fifty times slower. Here a
	// bar of the same volume and area (5.8 g as two equal grains, 12.5 cm2), 4 kg of 0.1 mol/kg HCl, no stirrer
	World::Builder b(builtin_species());
	const CellId cell = b.add_cell("beaker");
	const CellId bath = b.add_reservoir("bath", 298.15);
	b.add_edge(cell, bath, PHASES_NONE, 1e7);
	bool ok = false;
	b.set_amount(cell, water, amount_from_mol(4.0 * 55.508, ok));
	b.set_amount(cell, hydrogen_ion, amount_from_mol(0.4, ok)).set_amount(cell, chloride_ion, amount_from_mol(0.4, ok));
	b.set_amount(cell, magnesium, amount_from_mol(5.8 / 24.305, ok));
	b.set_particles(cell, magnesium, 2);
	b.set_temperature(cell, 298.15);
	std::string why;
	auto world = b.build(&why);
	REQUIRE_MESSAGE(world.has_value(), why);
	Simulation sim(std::move(*world), std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end()));
	StepTrace trace;
	const auto mg = [&] { return static_cast<double>(sim.world().amount(cell, magnesium).quanta) * MOL_PER_QUANTUM; };
	for (int step = 0; step < 600; ++step) { // ten seconds for the bubbles to start
		sim.step(trace);
	}
	const double before = mg();
	for (int step = 0; step < 1800; ++step) {
		sim.step(trace);
	}
	const Violations v = check_all(builtin_species(), trace, sim.equilibria());
	REQUIRE_MESSAGE(v.empty(), v.front());
	const double rate = (before - mg()) / 30.0; // mol Mg/s
	const CellState &state = sim.world().snapshot_cell(cell);
	const double d = particle_diameter(builtin_species(), state, magnesium);
	const double area = 2.0 * 3.141592653589793 * d * d;
	const double acid = static_cast<double>(state.amount[static_cast<size_t>(hydrogen_ion.index)]) * MOL_PER_QUANTUM / 4.0e-3; // mol/m3
	const double k = 2.0 * rate / area / acid; // the HCl's flux over its concentration, m/s
	// still liquid alone: Sherwood 2
	const SurfaceSteps still = surface_steps(builtin_species(), state, shipped(reaction::magnesium_in_acid), 298.15,
			[&] {
				bool defined = false;
				return affinity(builtin_species(), state, shipped(reaction::magnesium_in_acid).terms(), 298.15, defined);
			}(),
			0.0);
	const double k_still = 2.0 * still.both / acid;
	MESSAGE("magnesium in still 0.1 M HCl: k " << k * 6000.0 << " cm/min (Roald 0.20), still liquid alone " << k_still * 6000.0);
	CHECK(k > 0.5 * 0.20 / 6000.0);
	CHECK(k < 2.0 * 0.20 / 6000.0);
	CHECK(k > 10.0 * k_still);
}
