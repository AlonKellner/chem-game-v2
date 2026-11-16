// Refraction and Fresnel (GAME_SPEC §12.3, Stage 11): the indices, O5, O6, the lens a vessel of water
// is, the chord the light really takes, the rims, and the bench's drawing seen bent.
#include "lab/colour.hpp"
#include "lab/optics_scene.hpp"
#include "lab/ui.hpp"

#include "chem/builtin_ids.hpp"
#include "chem/optics.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <fstream>
#include <sstream>

using lab::ItemView;
using lab::OpticsScene;
using lab::SubstanceView;

namespace {

constexpr double WALL = 0.7305;
// a 250 mL vessel drawn 90 wide, its axis at x = 145
constexpr float X = 100, Y = 100, W = 90, H = 112.5f;
const int AXIS = 145, LOW = 200; // the pixel column just right of the axis (centre 145.5), a row deep in the liquid

lab::OpticsRoom room(bool refraction = true) {
	lab::OpticsRoom r;
	r.w = r.h = 1000;
	r.bench = WALL;
	r.elsewhere = 0.02;
	r.refraction = refraction;
	return r;
}

SubstanceView substance(const char *species, char phase, double mol, double g, bool wall = false) {
	SubstanceView s;
	s.species = species;
	s.phase = phase;
	s.mol = mol;
	s.g = g;
	s.wall = wall;
	return s;
}

// 100 mL of copper sulfate solution, with or without its 50 g of glass
ItemView vessel(double molar, bool glass) {
	ItemView v;
	v.id = "flask";
	v.kind = "vessel";
	v.volume_ml = 250.0;
	v.celsius = 20.0;
	v.liquid_ml = v.condensed_ml = 100.0;
	v.contents = { substance("water", 'l', 5.5, 100.0) };
	if (molar > 0.0) {
		v.contents.push_back(substance("copper_sulfate_aq", 'a', molar * 0.1, molar * 15.96));
	}
	if (glass) {
		v.contents.push_back(substance("glass", 's', 0.83, 50.0, true));
	}
	return v;
}

OpticsScene scene_of(const ItemView &v, const lab::OpticsRoom &r) {
	OpticsScene scene;
	lab::optics_compose(scene, r, { lab::OpticsBody{ &v, X, Y, W, H } });
	REQUIRE(lab::check_O1(scene) == "");
	return scene;
}

double index_at(chem::SpeciesId id, double nm) {
	const chem::optics::Spectrum &n = chem::optics::refractive_index(id);
	const auto i = static_cast<size_t>(nm - 380.0);
	const double f = nm - 380.0 - static_cast<double>(i);
	return n[i] * (1.0 - f) + n[i + 1] * f;
}

std::string read(const std::string &relative) {
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/" + relative);
	REQUIRE_MESSAGE(static_cast<bool>(in), relative);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

} // namespace

TEST_CASE("refraction: the indices of water and of laboratory glass, as measured (Stage 11)") {
	// water at 20 °C (Daimon and Masumura): the sodium D line's 1.3330
	CHECK(index_at(chem::builtin::water, 589.3) == doctest::Approx(1.3330).epsilon(2e-4));
	CHECK(index_at(chem::builtin::water, 450.0) > index_at(chem::builtin::water, 650.0)); // blue bends more
	// borosilicate 3.3 (Schott's BOROFLOAT 33 table), fitted at build within 2e-4 of every value
	const double table[][2] = { { 435.8, 1.48015 }, { 479.9, 1.47676 }, { 546.1, 1.47311 }, { 589.3, 1.47133 }, { 643.8, 1.46953 }, { 656.3, 1.46916 } };
	for (const auto &row : table) {
		CHECK(index_at(chem::builtin::glass, row[0]) == doctest::Approx(row[1]).epsilon(2e-4 / row[1]));
	}
	// its Abbe number, (ne - 1) / (nF' - nC'): 65.41
	const double abbe = (index_at(chem::builtin::glass, 546.1) - 1.0) / (index_at(chem::builtin::glass, 479.9) - index_at(chem::builtin::glass, 643.8));
	CHECK(abbe == doctest::Approx(65.41).epsilon(0.01));
	CHECK(chem::optics::index_density(chem::builtin::glass) == 2.23);
	CHECK(chem::optics::refractive_index(chem::builtin::copper_oxide)[100] == 1.0); // none known: air's
}

TEST_CASE("O5: where every index is 1, refraction changes nothing (Stage 11)") {
	const ItemView v = vessel(0.5, false); // no glass: with indices of 1 the light's path is the straight one
	OpticsScene bent = scene_of(v, room(true));
	const OpticsScene straight = scene_of(v, room(false));
	using namespace lab::optics_layout;
	for (const int row : { LIQUID_INDEX_ROW, WALL_INDEX_ROW }) {
		for (int i = 0; i < ROW; ++i) {
			bent.rows[static_cast<size_t>(row * ROW + i)] = 1.0f;
		}
	}
	double worst = 0.0;
	for (int y = LOW - 60; y <= LOW + 10; y += 7) {
		for (int x = static_cast<int>(X) - 1; x <= static_cast<int>(X + W); x += 3) {
			for (const double nm : { 420.0, 550.0, 700.0 }) {
				const double a = lab::optics_through(bent, x, y, nm), b = lab::optics_through(straight, x, y, nm);
				worst = std::max(worst, std::fabs(a - b) / std::max(b, 1e-300));
			}
		}
	}
	CHECK(worst < 1e-9);
	// and it fires: water's own index bends and reflects
	const OpticsScene water = scene_of(v, room(true));
	CHECK(std::fabs(lab::optics_through(water, AXIS + 30, LOW, 550.0) - lab::optics_through(straight, AXIS + 30, LOW, 550.0)) > 1e-3);
}

TEST_CASE("O6: Fresnel loses no light: in a room as bright in front as behind, clear glass and water change nothing (Stage 11)") {
	ItemView v = vessel(0.0, true);
	lab::OpticsRoom r = room(true);
	r.front = WALL;
	r.elsewhere = WALL; // the whole room one radiance: where a steeply bent ray lands does not matter
	OpticsScene scene = scene_of(v, r);
	using namespace lab::optics_layout;
	for (int i = 0; i < ROW; ++i) {
		scene.rows[static_cast<size_t>(LIQUID_ROW * ROW + i)] = 0.0f; // water that absorbs nothing
	}
	double worst = 0.0;
	for (int y = LOW - 80; y <= LOW + 10; y += 5) {
		for (int x = static_cast<int>(X); x < static_cast<int>(X + W); x += 2) {
			for (const double nm : { 400.0, 550.0, 760.0 }) {
				worst = std::max(worst, std::fabs(lab::optics_through(scene, x, y, nm) - WALL));
			}
			REQUIRE(lab::check_O2(scene, x, y) == "");
		}
	}
	CHECK(worst < 1e-7); // the scene's radiances are float32
}

TEST_CASE("refraction: a vessel of water is a lens: inverted and magnified, as paraxial optics says (Stage 11)") {
	const ItemView v = vessel(0.0, false);
	const OpticsScene scene = scene_of(v, room(true));
	const double cm_per_px = scene.table[static_cast<size_t>(lab::optics_layout::HEADER_VECS + lab::optics_layout::FORM) * 4 + 1];
	const double r_cm = 45.0 * cm_per_px, n = index_at(chem::builtin::water, 550.0), d_wall = 20.0;
	// near the axis the ray turns by 2 (n - 1) d / (n R) and lands at d - D turn: a magnification of 1 - 2 D (n - 1) / (n R)
	const double expected = 1.0 - 2.0 * d_wall * (n - 1.0) / (n * r_cm);
	const double slope = (lab::optics_bend(scene, AXIS + 1, LOW, 550.0).x - lab::optics_bend(scene, AXIS - 1, LOW, 550.0).x) / 2.0;
	MESSAGE("a 6.3 cm vessel of water 20 cm in front of the wall: the wall seen through it at " << slope << " times (paraxial: " << expected << ")");
	CHECK(expected < -1.0); // inverted, magnified
	CHECK(slope == doctest::Approx(expected).epsilon(0.02));
	// symmetric about the axis
	// (the axis is at 145.0, between pixels: pixel 145 + k mirrors pixel 144 - k)
	const double centre = AXIS;
	for (const int k : { 5, 15, 30 }) {
		CHECK(lab::optics_bend(scene, AXIS + k, LOW, 550.0).x - centre == doctest::Approx(-(lab::optics_bend(scene, AXIS - 1 - k, LOW, 550.0).x - centre)).epsilon(1e-9));
	}
	// dispersion: blue is bent further than red, so the inverted picture is stretched further in blue
	CHECK(std::fabs(lab::optics_bend(scene, AXIS + 10, LOW, 430.0).x - centre) > std::fabs(lab::optics_bend(scene, AXIS + 10, LOW, 680.0).x - centre));
	// above the liquid an empty vessel without glass bends nothing
	CHECK(lab::optics_bend(scene, AXIS + 10, static_cast<int>(Y) + 10, 550.0).x == doctest::Approx(AXIS + 10.5));
}

TEST_CASE("refraction: the light takes the bent chord through the liquid, at its own wavelength (Stage 11)") {
	// weak enough that some light gets through six centimetres at 680 nm
	const OpticsScene salt = scene_of(vessel(0.02, true), room(true)), plain = scene_of(vessel(0.0, true), room(true));
	const double tau_per_cm = chem::optics::spectrum(chem::builtin::copper_sulfate_aq)[300] * 2.302585092994046 * 0.02; // 680 nm, 0.02 mol/L
	for (const int k : { 0, 20, 35 }) {
		const lab::OpticsBend b = lab::optics_bend(salt, AXIS + k, LOW, 680.0);
		// through = pass * wall * T_water * T_salt + (1 - pass) * front: the same in both but T_salt, which is
		// exp(-tau L) along the chord the ray really crosses
		const double front = 0.3, reflected = (1.0 - b.pass) * front;
		const double t_salt = (lab::optics_through(salt, AXIS + k, LOW, 680.0) - reflected) / (lab::optics_through(plain, AXIS + k, LOW, 680.0) - reflected);
		CHECK(-std::log(t_salt) / tau_per_cm == doctest::Approx(b.chord).epsilon(1e-5));
		// the chord of a ray d from the axis inside water is 2 sqrt(r^2 - (d / n)^2): longer than the straight one
		const double cm_per_px = salt.table[static_cast<size_t>(lab::optics_layout::HEADER_VECS + lab::optics_layout::FORM) * 4 + 1];
		const double glass_cm = salt.table[static_cast<size_t>(lab::optics_layout::HEADER_VECS + lab::optics_layout::REFRACT) * 4];
		const double r_in = 45.0 * cm_per_px - glass_cm, d = (k + 0.5) * cm_per_px, n = index_at(chem::builtin::water, 680.0);
		CHECK(b.chord == doctest::Approx(2.0 * std::sqrt(r_in * r_in - (d / n) * (d / n))).epsilon(1e-6));
		if (k > 0) {
			CHECK(b.chord > 2.0 * std::sqrt(r_in * r_in - d * d));
		}
	}
	CHECK(salt.table[static_cast<size_t>(lab::optics_layout::HEADER_VECS + lab::optics_layout::REFRACT) * 4] > 0.05); // 50 g of glass: about a millimetre
}

TEST_CASE("refraction: glass shows the room in front at its rims, and lets most through face on (Stage 11)") {
	const OpticsScene scene = scene_of(vessel(0.0, true), room(true));
	const lab::OpticsBend middle = lab::optics_bend(scene, AXIS, LOW, 550.0), rim = lab::optics_bend(scene, static_cast<int>(X), LOW, 550.0);
	// face on: (1 - R_air-glass)^2 (1 - R_glass-water)^2, R = ((n1 - n2) / (n1 + n2))^2
	const double ng = index_at(chem::builtin::glass, 550.0), nw = index_at(chem::builtin::water, 550.0);
	const double r1 = std::pow((ng - 1.0) / (ng + 1.0), 2.0), r2 = std::pow((ng - nw) / (ng + nw), 2.0);
	CHECK(middle.pass == doctest::Approx((1 - r1) * (1 - r1) * (1 - r2) * (1 - r2)).epsilon(1e-3));
	CHECK(rim.pass < middle.pass - 0.2); // aslant, much is turned away
	CHECK(lab::optics_through(scene, static_cast<int>(X), LOW, 550.0) < lab::optics_through(scene, AXIS, LOW, 550.0) - 0.05); // so the rim is darker: the room in front
}

TEST_CASE("refraction: on the bench the vessels show the grid behind them, bent, and the scene holds O1 and O2 (Stage 11)") {
	std::string why;
	const std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/demo.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	ui->frame(0.0);
	const OpticsScene &scene = ui->optics();
	REQUIRE(lab::check_O1(scene) == "");
	CHECK(scene.table[2 * 4 + 2] == 1.0f); // refraction is on in the game
	int shapes = 0, bending = 0;
	using namespace lab::optics_layout;
	for (int i = 0; i < static_cast<int>(scene.table[0]); ++i) {
		const size_t at = static_cast<size_t>(HEADER_VECS + i * BODY_VECS);
		shapes += static_cast<int>(scene.table[(at + REFRACT) * 4 + 2]);
		bending += scene.table[(at + REFRACT) * 4 + 3] > 0.5f ? 1 : 0;
	}
	CHECK(shapes > 0); // the grid's lines behind them
	CHECK(bending >= 5); // the vessels; not the balloon
	// somewhere in a vessel the pixel's view lands on a grid line it does not lie on
	float fx = 0, fy = 0;
	REQUIRE(ui->locate("item:jar", fx, fy));
	int bent = 0;
	for (int x = static_cast<int>(fx) - 40; x <= static_cast<int>(fx) + 40; ++x) {
		const lab::OpticsBend b = lab::optics_bend(scene, x, static_cast<int>(fy), 550.0);
		bent += lab::optics_cover(scene, x, static_cast<int>(fy)) > 0.0 && std::fabs(b.x - (x + 0.5)) > 2.0 ? 1 : 0;
		REQUIRE(lab::check_O2(scene, x, static_cast<int>(fy)) == "");
	}
	CHECK(bent > 0);
}

TEST_CASE("refraction: the renderer's frames settle on a bent, dispersed view of the grid (Stage 11)") {
	ItemView v = vessel(0.3, true);
	lab::OpticsRoom r = room(true);
	for (int k = 0; k < 6; ++k) { // a grid line every 30 pixels behind the vessel
		r.shapes.push_back(lab::OpticsShape{ 60.0f + 30.0f * k, 0.0f, 1.0f, 1000.0f, 0.55 });
	}
	const OpticsScene scene = scene_of(v, r);
	lab::OpticsFrames frames(95, 95, 100, 125);
	for (int f = 0; f < 40; ++f) {
		frames.frame(scene);
	}
	double worst = 0.0, mean = 0.0;
	int covered = 0;
	for (int y = 95; y < 220; y += 2) {
		for (int x = 95; x < 195; x += 2) {
			if (lab::optics_cover(scene, x, y) <= 0.0) {
				continue;
			}
			double truth[3], now[3];
			lab::optics_truth(scene, x, y, truth);
			frames.colour(scene, x, y, now);
			const auto lab_of = [](const double c[3]) {
				return lab::shown(chem::optics::Xyz{ 0.4124564 * c[0] + 0.3575761 * c[1] + 0.1804375 * c[2], 0.2126729 * c[0] + 0.7151522 * c[1] + 0.0721750 * c[2],
						0.0193339 * c[0] + 0.1191920 * c[1] + 0.9503041 * c[2] });
			};
			const double e = lab::eye_difference(lab_of(truth), lab_of(now));
			worst = std::max(worst, e);
			mean += e;
			++covered;
		}
	}
	MESSAGE("settled through glass and solution, the grid bent behind: worst " << worst << ", mean " << mean / covered);
	CHECK(worst < lab::EYE_VISIBLE);
	CHECK(frames.points() == 0.0);
}
