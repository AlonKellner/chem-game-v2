// Measured kinetics (GAME_SPEC §7, Stage 23): each rate against its source, at the source's own
// conditions; the transport step from measured diffusion and water's viscosity.
#include "chem/builtin_ids.hpp"
#include "tables.hpp"
#include "chem/optics.hpp"
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

TEST_CASE("measured: the pentahydrate's first crystal appears at Sheng's induction times (Stage 23)") {
	// Sheng 2012 (PhD thesis, Leeds), Table 5.2: a litre saturated at 30 C, held at 26, 27 and 28 C (S = 1.080,
	// 1.058, 1.036 by concentration), first crystals after 6120-7200, 10600-20000 and 68000-84000 s. The fit counts
	// each as one nucleus in 0.98 kg of water at the geometric mean time (least squares): a lower bound. The first
	// crystal must appear inside each measured range.
	const struct {
		double celsius, s, first, last;
	} rows[] = { { 26.0, 1.080, 6120.0, 7200.0 }, { 27.0, 1.058, 10600.0, 20000.0 }, { 28.0, 1.036, 68000.0, 84000.0 } };
	const double thirty = saturation(303.15);
	for (const auto &row : rows) {
		CAPTURE(row.celsius);
		const double kelvin = row.celsius + 273.15, ms = saturation(kelvin);
		// the model's own supersaturation of Sheng's solution, against Sheng's
		CHECK(near(thirty / ms, row.s, 0.004));
		World::Builder b(builtin_species());
		const CellId cell = b.add_cell("flask");
		const CellId bath = b.add_reservoir("bath", kelvin);
		b.add_edge(cell, bath, PHASES_NONE, 1e6);
		const double water = 0.98 / 0.018015;
		b.set_amount(cell, builtin::water, Amount{ quanta(water) }).set_amount(cell, builtin::copper_sulfate_aq, Amount{ quanta(row.s * ms * 0.98) });
		b.set_temperature(cell, kelvin);
		std::string why;
		auto world = b.build(&why);
		REQUIRE_MESSAGE(world.has_value(), why);
		Simulation sim(std::move(*world), std::vector<Reaction>(builtin_reactions().begin(), builtin_reactions().end()));
		const double step = 10.0;
		sim.set_step_seconds(step);
		double seen = 0.0;
		for (int s = 1; s <= 12000 && seen == 0.0; ++s) {
			sim.step();
			if (sim.world().particles(cell, builtin::copper_sulfate_pentahydrate) > 0) {
				seen = s * step;
			}
		}
		REQUIRE(seen > 0.0);
		CHECK(seen >= row.first);
		CHECK(seen <= row.last);
		MESSAGE(row.celsius << " C: the first crystal after " << seen << " s (Sheng: " << row.first << "-" << row.last << " s)");
	}
}

TEST_CASE("measured: the copper sulfate standard's absorbancy is NBS Circular 484's, through the model's own spectrum (Stage 23)") {
	// Gibson 1949, Table 4: 20.000 g CuSO4.5H2O and 10.0 ml H2SO4 a litre (0.07986 mol/L of copper), 10.00 mm, 25 C;
	// the published absorbancy As every 10 nm, 380-750 nm (transcribed here apart from data/optics)
	const double as[] = { 0.0035, 0.0028, 0.0023, 0.0019, 0.0016, 0.0014, 0.0012, 0.0011, 0.0011, 0.0012, 0.0014, 0.0018, 0.0026,
		0.0038, 0.0055, 0.0079, 0.0111, 0.0155, 0.0216, 0.0292, 0.039, 0.0518, 0.068, 0.0885, 0.1125, 0.143, 0.18, 0.224, 0.274,
		0.332, 0.392, 0.459, 0.527, 0.592, 0.656, 0.715, 0.768, 0.817 };
	const optics::Spectrum &salt = optics::spectrum(builtin::copper_sulfate_aq);
	REQUIRE(optics::kind(builtin::copper_sulfate_aq) == optics::Kind::Absorb);
	const double molar = 0.07986, cm = 1.0;
	for (int i = 0; i < 38; ++i) {
		const int nm = 380 + 10 * i;
		CAPTURE(nm);
		CHECK(near(salt[static_cast<size_t>(nm - 380)] * molar * cm, as[i], 2e-4));
	}
	// between the rows, straight (650 and 660 nm)
	CHECK(near(salt[275] * molar, 0.5 * (0.224 + 0.274), 2e-4));
	// 760-780 nm: straight from NBS's 750 nm to Davenet et al.'s 12.05 L/mol/cm at 790 nm (interpolated, not measured)
	CHECK(near(salt[400], 0.817 / molar + (12.05 - 0.817 / molar) * 30.0 / 40.0, 2e-4));
}

TEST_CASE("measured: malachite reflects as the USGS library's GDS801, through the model's own spectrum (Stage 23)") {
	// splib07a Record 18922, 'Malachite GDS801 synthetic', absolute reflectance (transcribed here apart from data/optics)
	const struct {
		int nm;
		double reflectance;
	} rows[] = { { 380, 0.08832 }, { 450, 0.30105 }, { 520, 0.40302 }, { 600, 0.24569 }, { 650, 0.16524 }, { 700, 0.13033 }, { 780, 0.10932 } };
	const optics::Spectrum &m = optics::spectrum(builtin::malachite);
	REQUIRE(optics::kind(builtin::malachite) == optics::Kind::Reflect);
	for (const auto &row : rows) {
		CAPTURE(row.nm);
		CHECK(near(m[static_cast<size_t>(row.nm - 380)], row.reflectance, 1e-6));
	}
	// its one peak, near 520 nm as other samples have it: green
	size_t peak = 0;
	for (size_t i = 0; i < m.size(); ++i) {
		peak = m[i] > m[peak] ? i : peak;
	}
	CHECK(peak + 380 >= 510);
	CHECK(peak + 380 <= 530);
}

// ---- the long-wave infrared, 8-14 um (GAME_SPEC §12.1, Stage 46): each value at its source's own conditions

namespace {
// a grey body's band emissivity at normal incidence from its n + ik (opaque: what is not reflected is emitted),
// weighed by a black body at `kelvin` over the sensor's band
double band_emissivity(const optics::LwirSpectrum &n, const optics::LwirSpectrum &k, double kelvin) {
	double emitted = 0.0, all = 0.0;
	for (int i = 0; i < optics::LWIR_COUNT; ++i) {
		const double b = optics::planck(optics::lwir_nm(i), kelvin);
		emitted += b * (1.0 - optics::normal_reflectance(n[static_cast<size_t>(i)], k[static_cast<size_t>(i)]));
		all += b;
	}
	return emitted / all;
}
} // namespace

TEST_CASE("measured: water's long-wave index is Hale and Querry's at 10 um, and it is opaque in a hundredth of a millimetre (Stage 46)") {
	const SpeciesId water = builtin_species().find("water");
	const optics::LwirSpectrum *n = optics::lwir_n(water), *k = optics::lwir_k(water);
	REQUIRE(n != nullptr);
	REQUIRE(k != nullptr);
	const size_t at = (10000 - optics::LWIR_FIRST_NM) / optics::LWIR_STEP_NM;
	CHECK((*n)[at] == doctest::Approx(1.218).epsilon(1e-12)); // Hale and Querry, Appl. Opt. 12, 555 (1973), at 10.0 um
	CHECK((*k)[at] == doctest::Approx(0.0508).epsilon(1e-12));
	CHECK((*k)[(12000 - optics::LWIR_FIRST_NM) / optics::LWIR_STEP_NM] == doctest::Approx(0.199).epsilon(1e-12));
	// 4 pi k / wavelength: 638 per cm at 10 um, so 0.1 mm of water passes e^-6.4
	CHECK(optics::absorption_per_cm((*k)[at], 10000.0) == doctest::Approx(638.37).epsilon(1e-4));
	// its emissivity over the band: at least Fluke's table's 0.93 for water (a guide: theirs is not at normal incidence)
	const double e = band_emissivity(*n, *k, 295.0);
	CHECK(e >= 0.93);
	CHECK(e < 1.0);
}

TEST_CASE("measured: glass's long-wave reflectance peaks as Kitamura's fused silica does, in its Reststrahlen band (Stage 46)") {
	const SpeciesId glass = builtin_species().find("glass");
	const optics::LwirSpectrum *n = optics::lwir_n(glass), *k = optics::lwir_k(glass);
	REQUIRE(n != nullptr);
	REQUIRE(k != nullptr);
	double most = 0.0, at_nm = 0.0;
	for (int i = 0; i < optics::LWIR_COUNT; ++i) {
		const double r = optics::normal_reflectance((*n)[static_cast<size_t>(i)], (*k)[static_cast<size_t>(i)]);
		if (r > most) {
			most = r;
			at_nm = optics::lwir_nm(i);
		}
	}
	// Kitamura, Pilon and Jonasz, Appl. Opt. 46, 8118 (2007), fitted to Popova et al. (1972): n 0.45931, k 1.9693 at
	// 8929.5 nm, a normal reflectance of 0.6942, the band's most
	CHECK(most == doctest::Approx(0.6942).epsilon(0.01));
	CHECK(std::fabs(at_nm - 8929.5) <= optics::LWIR_STEP_NM);
	// opaque: a micrometre at the peak absorbs nearly everything, and a vessel's millimetre of wall all of it
	const size_t at = (9000 - optics::LWIR_FIRST_NM) / optics::LWIR_STEP_NM;
	CHECK(optics::absorption_per_cm((*k)[at], 9000.0) > 1e4);
	CHECK(optics::absorption_per_cm((*k)[(11000 - optics::LWIR_FIRST_NM) / optics::LWIR_STEP_NM], 11000.0) > 1e3);
	// its emissivity over the band, within 0.1 of Fluke's table's 0.85 for plate glass (a soda-lime glass, not silica)
	CHECK(std::fabs(band_emissivity(*n, *k, 295.0) - 0.85) < 0.1);
	// the index the pass bends by gives the same reflectance at normal incidence
	for (int i = 0; i < optics::LWIR_COUNT; ++i) {
		const double nn = (*n)[static_cast<size_t>(i)], kk = (*k)[static_cast<size_t>(i)], m = optics::effective_index(nn, kk);
		CHECK(m >= 1.0);
		CHECK(optics::normal_reflectance(m, 0.0) == doctest::Approx(optics::normal_reflectance(nn, kk)).epsilon(1e-12));
	}
}

TEST_CASE("measured: each long-wave emissivity is its table's: Fluke's for plastics, rubber, glass and oxidised steel (Stage 46)") {
	// Fluke Process Instruments, emissivity of non-metals and of metals, 8-14 um (a guide)
	for (const char *plastic : { "timer_face", "monitor_face", "part_face", "wire", "cap_plastic", "viewer_case", "probe_cable" }) {
		CAPTURE(plastic);
		CHECK(optics::lwir_emissivity(plastic).value_or(-1.0) == 0.95); // plastic, opaque
	}
	CHECK(optics::lwir_emissivity("rubber").value_or(-1.0) == 0.95);
	CHECK(optics::lwir_emissivity("lamp_glass").value_or(-1.0) == 0.85); // glass, plate
	const double steel = optics::lwir_emissivity("burner_metal").value_or(-1.0);
	CHECK(steel >= 0.7); // steel, oxidised: 0.7 to 0.9
	CHECK(steel <= 0.9);
	CHECK(!optics::lwir_emissivity("no such thing").has_value());
}

TEST_CASE("measured: Planck's law at 10 um and 300 K, and the band's signal as its integral (Stage 46)") {
	// a black body at 300 K, 10 um: 9.924 W / m2 / sr / um
	CHECK(optics::planck(10000.0, 300.0) == doctest::Approx(9.924e-3).epsilon(1e-3));
	// the closed form against the integral itself (Simpson's rule at a nanometre)
	for (const double kelvin : { 230.0, 295.0, 373.15, 900.0, 2200.0 }) {
		CAPTURE(kelvin);
		double sum = 0.0;
		for (int i = 0; i <= 6000; ++i) {
			sum += optics::planck(8000.0 + i, kelvin) * (i == 0 || i == 6000 ? 1.0 : i % 2 == 1 ? 4.0 : 2.0) / 3.0;
		}
		CHECK(optics::band_signal(kelvin) == doctest::Approx(sum).epsilon(1e-9));
	}
}

TEST_CASE("measured: a camera's temperature from its signal round-trips to a millikelvin, any emissivity, any reflection (Stage 46)") {
	for (double kelvin = 230.0; kelvin <= 2500.0; kelvin *= 1.37) {
		for (const double e : { 0.1, 0.5, 0.95, 1.0 }) {
			for (const double reflected : { 250.0, 295.0, 330.0 }) {
				CAPTURE(kelvin);
				CAPTURE(e);
				CAPTURE(reflected);
				const double signal = e * optics::band_signal(kelvin) + (1.0 - e) * optics::band_signal(reflected);
				CHECK(std::fabs(optics::brightness_kelvin(signal, e, reflected) - kelvin) < 1e-3);
			}
		}
	}
	// the room's light as the sensor weighs it: shares of its signal, the whole of it 1
	const optics::EnergyTable t = optics::lwir_energy_table(295.0);
	CHECK(t.entry.front().nm == doctest::Approx(8000.0));
	CHECK(t.entry.back().nm == doctest::Approx(14000.0));
	CHECK(t.total.y == 1.0);
	CHECK(t.at(0.5).up_to.y == doctest::Approx(0.5));
	// half the signal of a room at 295 K lies below its median wavelength
	double below = 0.0;
	const double median = t.at(0.5).nm;
	for (double nm = 8000.25; nm < 14000.0; nm += 0.5) {
		below += nm < median ? optics::planck(nm, 295.0) * 0.5 : 0.0;
	}
	CHECK(below / optics::band_signal(295.0) == doctest::Approx(0.5).epsilon(1e-3));
}
