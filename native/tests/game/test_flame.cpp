// A flame's light from what is in it (GAME_SPEC §12, Stage 82): each element's lines by Boltzmann's populations at the
// flame's temperature (NIST's), its molecule's bands; the glow keeps each line's light and its centre; a flame test
// fills a lit burner's flame and it fades; the picture shows a sodium flame yellow and the spectral viewer its D lines.
#include "lab/colour.hpp"
#include "lab/flame.hpp"
#include "lab/lamps.hpp"
#include "lab/lab.hpp"
#include "lab/optics_scene.hpp"
#include "lab/ui.hpp"

#include "chem/optics.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <memory>
#include <string>

namespace {

std::unique_ptr<lab::Lab> open(const std::string &bench) {
	std::string why;
	auto l = lab::Lab::open(bench, why);
	REQUIRE_MESSAGE(l != nullptr, why);
	return l;
}

void ok(lab::Lab &l, const std::string &c) {
	std::string why;
	REQUIRE_MESSAGE(l.command(c, why), c << ": " << why);
}

lab::ItemView item(lab::Lab &l, const std::string &id) {
	for (const lab::ItemView &v : l.view().items) {
		if (v.id == id) {
			return v;
		}
	}
	FAIL("no " << id);
	return lab::ItemView{};
}

double load_of(const lab::ItemView &v, const std::string &el) {
	for (const auto &[e, mol] : v.flame_load) {
		if (e == el) {
			return mol;
		}
	}
	return 0.0;
}

// a lab burner (1927 C, the benches') and a beaker of 0.1 M sodium chloride, one of strontium chloride, one of water
const char *const BENCH = R"({"format": "chem-bench/2", "room": {"celsius": 20, "air_mol": 1000}, "items": [
	{"id": "salt", "at": [6, 7], "kind": "vessel", "volume_ml": 50, "wall": "glass", "glassware": "beaker", "wall_g": 40,
		"contents": {"water": {"g": 20}, "sodium_chloride_aq": {"mol": 0.002}}},
	{"id": "strontium", "at": [10, 7], "kind": "vessel", "volume_ml": 50, "wall": "glass", "glassware": "beaker", "wall_g": 40,
		"contents": {"water": {"g": 20}, "strontium_chloride_aq": {"mol": 0.002}}},
	{"id": "water", "at": [14, 7], "kind": "vessel", "volume_ml": 50, "wall": "glass", "glassware": "beaker", "wall_g": 40,
		"contents": {"water": {"g": 20}}},
	{"id": "burner", "kind": "burner", "celsius": 1927, "conductance": 0.25}]})";

constexpr double HC_K = 1.438776877; // cm K

} // namespace

TEST_CASE("flame: each line's share is Boltzmann's at the flame's temperature, NIST's A and g; a molecule's bands carry their share (Stage 82)") {
	const double T = 1927.0 + 273.15;
	const std::vector<lab::flame::Part> na = lab::flame::parts("Na", T);
	double total = 0.0;
	for (const lab::flame::Part &p : na) {
		total += p.share;
	}
	CHECK(total == doctest::Approx(1.0).epsilon(1e-12));
	// NIST: D2 588.995 (A 6.16e7, g 4), D1 589.592 (6.14e7, 2): nearly at one energy, in their g A / lambda ratio
	const auto share_at = [](const std::vector<lab::flame::Part> &ps, double nm) {
		for (const lab::flame::Part &p : ps) {
			if (std::abs(p.nm - nm) < 0.002) {
				return p.share;
			}
		}
		return 0.0;
	};
	const double d2 = share_at(na, 588.995095), d1 = share_at(na, 589.592424);
	CHECK(d2 / d1 == doctest::Approx((4.0 * 6.16e7 * std::exp(-HC_K * 16973.36619 / T) / 588.995095) / (2.0 * 6.14e7 * std::exp(-HC_K * 16956.17025 / T) / 589.592424)).epsilon(1e-9));
	// 568.82 nm (3p - 4d, E 34548.729, g 6, A 1.21e7) against D2: Boltzmann's factor across 17575 cm-1, about 1e-5
	const double far = share_at(na, 568.82046);
	CHECK(far / d2 == doctest::Approx((6.0 * 1.21e7 * std::exp(-HC_K * 34548.729 / T) / 568.82046) / (4.0 * 6.16e7 * std::exp(-HC_K * 16973.36619 / T) / 588.995095)).epsilon(1e-9));
	CHECK(far / d2 < 1e-4);
	// strontium: its SrOH bands carry 0.97 of its light at the burner's temperature (calibrated: its atoms mostly bound
	// as SrOH, its flame crimson), its atoms' lines the rest
	double banded = 0.0;
	for (const lab::flame::Part &p : lab::flame::parts("Sr", T)) {
		banded += p.fwhm > 0.0 ? p.share : 0.0;
	}
	CHECK(banded == doctest::Approx(0.97).epsilon(0.01));
	// copper: its atoms barely excited at 2200 K, its light its CuOH's, half sodium's
	CHECK(lab::flame::brightness("Cu", T) == doctest::Approx(0.5).epsilon(0.01));
	// a hotter flame is brighter; calcium's atoms (its resonance line higher) a tenth of sodium's or less, its light
	// mostly its CaOH bands' (0.9 of it)
	CHECK(lab::flame::brightness("Na", T + 200.0) > lab::flame::brightness("Na", T));
	CHECK(lab::flame::brightness("Na", T) == doctest::Approx(1.0).epsilon(1e-12));
	double calcium_atoms = 0.0;
	for (const lab::flame::Part &p : lab::flame::parts("Ca", T)) {
		calcium_atoms += p.fwhm > 0.0 ? 0.0 : p.share * lab::flame::brightness("Ca", T);
	}
	CHECK(calcium_atoms < 0.1);
	CHECK(!lab::flame::shows("Cl"));
}

TEST_CASE("flame: its glow keeps each line's light and its centre, as a multiple of the room's light (Stage 82)") {
	const double T = 2200.15;
	const std::array<double, 401> g = lab::flame::glow({ { "Na", 1e-6 } }, T);
	const chem::optics::Spectrum &room = chem::optics::d65();
	double light = 0.0;
	for (int k = 0; k < 401; ++k) {
		light += g[static_cast<size_t>(k)] * room[static_cast<size_t>(k)] / room[180]; // back to the room's white at 560 nm
	}
	// all of it: per_mol (1.2e8) times a micromole, sodium's brightness 1 at the burner's temperature
	CHECK(light == doctest::Approx(120.0).epsilon(1e-9));
	// the D lines' light centred where they are (most of it in them): between 588.995 and 589.592
	double d = 0.0, dm = 0.0;
	for (int k = 208; k <= 210; ++k) {
		const double p = g[static_cast<size_t>(k)] * room[static_cast<size_t>(k)] / room[180];
		d += p;
		dm += p * (380.0 + k);
	}
	CHECK(d > 0.999 * light);
	CHECK(dm / d > 588.995);
	CHECK(dm / d < 589.592);
	// nothing where it has no line
	CHECK(g[100] == 0.0);
}

TEST_CASE("flame: a flame test fills a lit burner's flame with what the drop carries, it fades by its 1/e time, and a save keeps it (Stage 82)") {
	auto l = open(BENCH);
	std::string why;
	// an unlit burner refuses
	CHECK(!l->command(R"({"do": "flame_test", "burner": "burner", "from": "salt"})", why));
	CHECK(why.find("not lit") != std::string::npos);
	ok(*l, R"({"do": "valve", "burner": "burner", "to": 1})");
	ok(*l, R"({"do": "flame_test", "burner": "burner", "from": "salt"})");
	// a 0.02 mL drop of 0.1 mol/kg (its liquid about 20.1 mL): about 2 umol of sodium; no chlorine shown
	const double na = load_of(item(*l, "burner"), "Na");
	CHECK(na == doctest::Approx(0.02e-3 / 20.1e-3 * 0.002).epsilon(0.02));
	CHECK(item(*l, "burner").flame_load.size() == 1);
	// the vessel keeps its matter (the drop too small to count)
	CHECK(item(*l, "salt").liquid_ml > 20.0);
	// it fades: after 3 s, e^-1 of it
	ok(*l, R"({"do": "wait", "seconds": 3})");
	CHECK(load_of(item(*l, "burner"), "Na") == doctest::Approx(na * std::exp(-1.0)).epsilon(0.02));
	// two metals at once; water adds nothing
	ok(*l, R"({"do": "flame_test", "burner": "burner", "from": "strontium"})");
	ok(*l, R"({"do": "flame_test", "burner": "burner", "from": "water"})");
	CHECK(load_of(item(*l, "burner"), "Sr") > 0.0);
	CHECK(item(*l, "burner").flame_load.size() == 2);
	// a save keeps what is in the flame, and the digest follows it
	const std::string saved = l->save();
	auto again = lab::Lab::load(saved, why);
	REQUIRE_MESSAGE(again != nullptr, why);
	CHECK(again->digest() == l->digest());
	CHECK(load_of(item(*again, "burner"), "Sr") == load_of(item(*l, "burner"), "Sr"));
	// long after, gone
	ok(*l, R"({"do": "wait", "seconds": 120})");
	CHECK(item(*l, "burner").flame_load.empty());
}

TEST_CASE("flame: a sodium flame is drawn yellow, and the spectral viewer sees its D lines (Stage 82)") {
	// a flame of the lab's burner, wide open, with a micromole of sodium in it, alone in a room
	const std::array<double, 401> sodium = lab::flame::glow({ { "Na", 2e-6 } }, 2200.15);
	lab::OpticsBody cone;
	cone.shape = lab::OpticsBody::Cone;
	cone.x = 300.0f;
	cone.y = 200.0f;
	cone.w = 24.0f;
	cone.h = 42.0f;
	cone.light = "flame_sooty";
	cone.light_to = "flame_blue";
	cone.toward = 1.0;
	cone.kelvin = 2200.15;
	cone.id = 5;
	lab::OpticsRoom room;
	room.w = room.h = 1000;
	room.bench = 0.7305;
	room.elsewhere = 0.02;
	lab::OpticsScene plain, yellow;
	lab::optics_compose(plain, room, { cone });
	cone.glow_add = &sodium;
	lab::optics_compose(yellow, room, { cone });
	CHECK(lab::check_O1(yellow) == "");
	CHECK(lab::check_O2(yellow, 312, 230) == "");
	CHECK(lab::check_O2(yellow, 306, 215) == "");
	// the viewer's reading through the flame's middle: at 589 nm far above its neighbours and above the flame alone
	const double at589 = lab::optics_point(yellow, 312.0, 230.0, 589.0), at580 = lab::optics_point(yellow, 312.0, 230.0, 580.0);
	MESSAGE("through the flame: 589 nm " << at589 << ", 580 nm " << at580 << ", without sodium " << lab::optics_point(plain, 312.0, 230.0, 589.0));
	CHECK(at589 > 10.0 * at580);
	CHECK(at589 > 5.0 * lab::optics_point(plain, 312.0, 230.0, 589.0));
	// and the picture there: yellow (red and green well over blue), redder than the flame without it
	double with[3], without[3];
	lab::optics_area_truth(yellow, 312, 230, 4, with);
	lab::optics_area_truth(plain, 312, 230, 4, without);
	MESSAGE("the flame's pixel: with sodium " << with[0] << " " << with[1] << " " << with[2] << ", without " << without[0] << " " << without[1] << " " << without[2]);
	CHECK(with[0] > 1.5 * with[2]);
	CHECK(with[1] > 1.2 * with[2]);
	// shown (Stage 82: brighter than the display, scaled by its brightest channel): a saturated orange-yellow, its red
	// the display's brightest, green a fraction of it, no blue
	lab::gamut_map(with);
	MESSAGE("shown: " << with[0] << " " << with[1] << " " << with[2]);
	CHECK(with[0] == doctest::Approx(1.0).epsilon(0.01));
	CHECK(with[1] > 0.15);
	CHECK(with[1] < 0.6);
	CHECK(with[2] < 0.05);
}

TEST_CASE("measured: a flame's lines are NIST's at the burner's temperature, its molecules' bands Meyerriecks and Kosanke's (Stage 82)") {
	const double T = 1927.0 + 273.15;
	// the strongest of each element's parts (a line, or a molecule's band) at the lab burner's temperature
	const auto strongest = [&](const std::string &el, bool band) {
		double nm = 0.0, best = -1.0;
		for (const lab::flame::Part &p : lab::flame::parts(el, T)) {
			if ((p.fwhm > 0.0) == band && p.share > best) {
				best = p.share;
				nm = p.nm;
			}
		}
		return nm;
	};
	// the flame tests' lines (NIST ASD): sodium's D lines, potassium's red doublet, lithium's red resonance, calcium's
	// 422.7, strontium's 460.7, barium's 553.5, copper's 510.6
	CHECK(strongest("Na", false) == doctest::Approx(588.995).epsilon(1e-5));
	CHECK(strongest("K", false) == doctest::Approx(766.490).epsilon(1e-5));
	CHECK(strongest("Li", false) == doctest::Approx(670.78).epsilon(1e-4));
	CHECK(strongest("Ca", false) == doctest::Approx(422.673).epsilon(1e-5));
	CHECK(strongest("Sr", false) == doctest::Approx(460.733).epsilon(1e-5));
	CHECK(strongest("Ba", false) == doctest::Approx(553.548).epsilon(1e-5));
	CHECK(strongest("Cu", false) == doctest::Approx(510.554).epsilon(1e-5));
	// the bands' peaks (their Table 2): CaOH 625, SrOH 682, BaOH 513, CuOH 546 nm
	CHECK(strongest("Ca", true) == doctest::Approx(625.0));
	CHECK(strongest("Sr", true) == doctest::Approx(682.0));
	CHECK(strongest("Ba", true) == doctest::Approx(513.0));
	CHECK(strongest("Cu", true) == doctest::Approx(546.0));
	// the alkalis' doublets 2 : 1 (their upper levels' g, 4 against 2, as a thin flame shows them)
	double d2 = 0.0, d1 = 0.0, k2 = 0.0, k1 = 0.0;
	for (const lab::flame::Part &p : lab::flame::parts("Na", T)) {
		d2 += std::abs(p.nm - 588.995) < 0.01 ? p.share : 0.0;
		d1 += std::abs(p.nm - 589.592) < 0.01 ? p.share : 0.0;
	}
	for (const lab::flame::Part &p : lab::flame::parts("K", T)) {
		k2 += std::abs(p.nm - 766.490) < 0.01 ? p.share : 0.0;
		k1 += std::abs(p.nm - 769.896) < 0.01 ? p.share : 0.0;
	}
	CHECK(d2 / d1 == doctest::Approx(2.0).epsilon(0.02));
	CHECK(k2 / k1 == doctest::Approx(2.0).epsilon(0.03));
}

TEST_CASE("flame: on the screen, a vessel of a solution carried onto a lit burner is a flame test (Stage 82)") {
	std::string why;
	auto ui = lab::Ui::open(BENCH, why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	const auto drop = [&](const std::string &what, const std::string &onto) {
		float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		REQUIRE(ui->locate("item:" + what, x0, y0));
		REQUIRE(ui->locate("item:" + onto, x1, y1));
		ui->pointer(x0, y0, false);
		ui->pointer(x0, y0, true);
		ui->pointer(x0, y0, false);
		ui->pointer(x1, y1, false);
		ui->pointer(x1, y1, true);
		ui->pointer(x1, y1, false);
	};
	const auto sodium = [&] { return load_of(item(ui->lab(), "burner"), "Na"); };
	// the burner is in Supplies until it is set out; set out, unlit: nothing goes into its flame
	ok(ui->lab(), R"({"do": "place", "item": "burner", "at": [20, 7]})");
	drop("salt", "burner");
	CHECK(sodium() == 0.0);
	// lit: the drop's sodium in its flame, the vessel back where it stood
	const auto at = item(ui->lab(), "salt");
	ok(ui->lab(), R"({"do": "valve", "burner": "burner", "to": 1})");
	drop("salt", "burner");
	CHECK(sodium() > 1e-6);
	CHECK(item(ui->lab(), "salt").col == at.col);
	CHECK(item(ui->lab(), "salt").row == at.row);
}

TEST_CASE("measured: a discharge lamp's lines are NIST's, at the relative intensities its sources observed (Stage 85)") {
	const auto strongest = [](const std::string &gas) {
		std::array<double, 2> best{};
		for (const auto &l : lab::lamps::lines(gas)) {
			best = l[1] > best[1] ? l : best;
		}
		return best[0];
	};
	const auto intensity = [](const std::string &gas, double nm) {
		for (const auto &l : lab::lamps::lines(gas)) {
			if (std::abs(l[0] - nm) < 0.01) {
				return l[1];
			}
		}
		return 0.0;
	};
	// hydrogen's Balmer lines: H-alpha 656.279 the strongest, then H-beta 486.135, H-gamma 434.047, H-delta 410.173
	CHECK(strongest("H") == doctest::Approx(656.279).epsilon(1e-6));
	CHECK(intensity("H", 656.279) > intensity("H", 486.135));
	CHECK(intensity("H", 486.135) > intensity("H", 434.047));
	CHECK(intensity("H", 434.047) > intensity("H", 410.173));
	// the Rydberg formula through them: 1/lambda = R (1/4 - 1/n^2), in air (n 1.000277 at 656 nm)
	for (const auto &[n, nm] : { std::pair{ 3, 656.279 }, std::pair{ 4, 486.135 }, std::pair{ 5, 434.047 }, std::pair{ 6, 410.173 } }) {
		CHECK(intensity("H", nm) > 0.0);
		const double r = 1.0 / (nm * 1.000277e-9 * (0.25 - 1.0 / (n * n)));
		CHECK(r == doctest::Approx(1.09678e7).epsilon(2e-4)); // hydrogen's own, R_H = R_inf / (1 + m_e / m_p)
	}
	// helium's yellow 587.56; mercury's violet, blue and green 404.66, 435.83, 546.07; neon's red 692.95
	CHECK(intensity("He", 587.562) > 0.0);
	CHECK(strongest("Hg") == doctest::Approx(404.656).epsilon(1e-5));
	CHECK(intensity("Hg", 435.834) == intensity("Hg", 404.656));
	CHECK(intensity("Hg", 546.075) > 0.0);
	CHECK(strongest("Ne") == doctest::Approx(692.947).epsilon(1e-5));
	// a lamp's glow keeps its light: all its lines' together, its model's, at full power
	const std::array<double, 401> g = lab::lamps::glow("Hg", 1.0);
	const chem::optics::Spectrum &room = chem::optics::d65();
	double light = 0.0;
	for (size_t k = 0; k < 401; ++k) {
		light += g[k] * room[k] / room[180];
	}
	CHECK(light == doctest::Approx(240.0).epsilon(1e-9));
	CHECK(lab::lamps::glow("Hg", 0.5)[25] == doctest::Approx(0.5 * g[25]));
}
