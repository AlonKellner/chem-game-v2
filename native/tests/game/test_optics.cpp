// The optical pass (GAME_SPEC §12, §12.2): the scene the renderer is given, and
// the reference renderer, which is the GPU's own per-pixel source compiled as C++:
// each pixel's colour, and the renderer's frames that settle on it.
#include "lab/colour.hpp"
#include "lab/optics_scene.hpp"
#include "lab/ui.hpp"

#include "chem/builtin_ids.hpp"
#include "chem/optics.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <sstream>
#include <thread>

using lab::ItemView;
using lab::OpticsScene;
using lab::SubstanceView;

namespace {

constexpr double BACKDROP = 0.7305;
// absorption, reflection and emission alone: nothing bends (refraction has its own tests, test_refraction.cpp)
const lab::OpticsRoom ROOM = [] {
	lab::OpticsRoom r;
	r.w = r.h = 1000;
	r.bench = BACKDROP;
	r.elsewhere = 0.02;
	r.refraction = false;
	return r;
}();
// a 250 mL vessel drawn 90 wide: 6.338 cm across
constexpr float X = 100, Y = 100, W = 90, H = 112.5f;
const double DIAMETER_CM = std::cbrt(4.0 * 250.0 / (3.14159265358979323846 * 1.25));

std::string read(const std::string &relative) {
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/" + relative);
	REQUIRE_MESSAGE(static_cast<bool>(in), relative);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

SubstanceView substance(const char *species, char phase, double mol, double g, int64_t particles = 0, double size_um = 0.0) {
	SubstanceView s;
	s.species = species;
	s.phase = phase;
	s.mol = mol;
	s.g = g;
	s.particles = particles;
	s.size_um = size_um;
	return s;
}

ItemView vessel() {
	ItemView v;
	v.id = "flask";
	v.kind = "vessel";
	v.volume_ml = 250.0;
	v.celsius = 20.0;
	return v;
}

// 100 mL of copper sulfate solution
ItemView solution(double molar) {
	ItemView v = vessel();
	v.liquid_ml = v.condensed_ml = 100.0;
	v.contents = { substance("water", 'l', 5.5, 100.0), substance("copper_sulfate_aq", 'a', molar * 0.1, molar * 15.96) };
	return v;
}

OpticsScene scene_of(const ItemView &v, float w = W, float h = H) {
	OpticsScene scene;
	lab::optics_compose(scene, ROOM, { lab::OpticsBody{ &v, X, Y, w, h } });
	REQUIRE(lab::check_O1(scene) == "");
	return scene;
}

lab::CieLab lab_of(const double rgb[3]) {
	return lab::to_lab(chem::optics::Xyz{ 0.4124564 * rgb[0] + 0.3575761 * rgb[1] + 0.1804375 * rgb[2], 0.2126729 * rgb[0] + 0.7151522 * rgb[1] + 0.0721750 * rgb[2],
			0.0193339 * rgb[0] + 0.1191920 * rgb[1] + 0.9503041 * rgb[2] });
}

// the colour pixel (x, y) should show
lab::CieLab seen(const OpticsScene &scene, int x, int y) {
	double rgb[3];
	lab::optics_truth(scene, x, y, rgb);
	return lab_of(rgb);
}

// the two as the screen shows them, on the eye metric
double eye_between(const double a[3], const double b[3]) {
	const auto xyz = [](const double c[3]) {
		return chem::optics::Xyz{ 0.4124564 * c[0] + 0.3575761 * c[1] + 0.1804375 * c[2], 0.2126729 * c[0] + 0.7151522 * c[1] + 0.0721750 * c[2], 0.0193339 * c[0] + 0.1191920 * c[1] + 0.9503041 * c[2] };
	};
	return lab::eye_difference(lab::shown(xyz(a)), lab::shown(xyz(b)));
}

// how far what the renderer's frames show is from the truth, at one pixel
double frames_error(const OpticsScene &scene, const lab::OpticsFrames &frames, int x, int y) {
	double truth[3], now[3];
	lab::optics_truth(scene, x, y, truth);
	frames.colour(scene, x, y, now);
	return eye_between(truth, now);
}

// the truth against the 1 nm reference: the same physics, integrated two ways
constexpr double TRUTH_FIT = 0.3;

// the 1 nm colour of the backdrop behind `absorbance` of the dissolved salt, or of a solid under it
lab::CieLab reference(double absorbance_scale, const chem::optics::Spectrum *reflectance = nullptr) {
	const chem::optics::Spectrum &salt = chem::optics::spectrum(chem::builtin::copper_sulfate_aq);
	chem::optics::Spectrum factor;
	for (size_t i = 0; i < factor.size(); ++i) {
		factor[i] = (reflectance != nullptr ? (*reflectance)[i] : BACKDROP) * std::pow(10.0, -salt[i] * absorbance_scale);
	}
	return lab::to_lab(chem::optics::colour(factor));
}

const int FLOOR = static_cast<int>(Y + H) - 1; // the row of pixels on the vessel's floor
const int MIDDLE = static_cast<int>(X + W / 2); // the column whose centre is the vessel's axis, within half a pixel

} // namespace

TEST_CASE("optics: an empty vessel changes nothing, and outside it there is nothing (Stage 10)") {
	const ItemView v = vessel();
	const OpticsScene scene = scene_of(v);
	double rgba[4];
	lab::optics_pixel(scene, MIDDLE, Y + 50, rgba);
	CHECK(rgba[3] == 1.0);
	for (int c = 0; c < 3; ++c) {
		CHECK(std::lround(rgba[c] * 255.0) == 0xde); // the wall's own colour: no seam around a vessel
	}
	lab::optics_pixel(scene, X - 20, Y + 50, rgba);
	CHECK(rgba[3] == 0.0);
	lab::optics_pixel(scene, MIDDLE, Y + H + 20, rgba);
	CHECK(rgba[3] == 0.0);
	// clear water is as clear
	ItemView water = vessel();
	water.liquid_ml = water.condensed_ml = 100.0;
	water.contents = { substance("water", 'l', 5.5, 100.0) };
	lab::optics_pixel(scene_of(water), MIDDLE, FLOOR - 5, rgba);
	CHECK(std::lround(rgba[1] * 255.0) == 0xde);
}

TEST_CASE("UX4: a solution's colour is that of its concentration and of the path through it (Stage 10)") {
	for (const double molar : { 0.05, 0.2, 0.5, 1.3, 3.0 }) {
		const ItemView v = solution(molar);
		const OpticsScene scene = scene_of(v);
		// through the middle: the whole diameter
		const lab::CieLab middle = seen(scene, MIDDLE, FLOOR - 10);
		CHECK(lab::difference(middle, reference(molar * DIAMETER_CM)) <= TRUTH_FIT);
		// a quarter of the way in from the rim the chord is sqrt(3)/2 of it
		const int x = static_cast<int>(X + W / 4);
		CHECK(lab::difference(seen(scene, x, FLOOR - 10), reference(molar * DIAMETER_CM * std::sqrt(3.0) / 2.0)) <= TRUTH_FIT);
		// above the surface, nothing
		CHECK(lab::difference(seen(scene, MIDDLE, static_cast<int>(Y) + 10), reference(0.0)) < 0.01); // the light integrated two ways: 0.002
		CHECK(middle.b < -1.0); // blue
	}
}

TEST_CASE("optics: the blue deepens with concentration and with depth (Stage 10)") {
	double before = 101.0;
	for (const double molar : { 0.0, 0.05, 0.2, 0.5, 1.3, 3.0 }) {
		const ItemView v = solution(molar);
		const lab::CieLab c = seen(scene_of(v), MIDDLE, FLOOR - 10);
		CHECK(c.l < before);
		before = c.l;
	}
	// the same solution is paler near the rim, where there is less of it to look through
	const ItemView v = solution(0.5);
	const OpticsScene scene = scene_of(v);
	CHECK(seen(scene, X + 4, FLOOR - 10).l > seen(scene, X + 20, FLOOR - 10).l);
	CHECK(seen(scene, X + 20, FLOOR - 10).l > seen(scene, MIDDLE, FLOOR - 10).l);
	// the same amount of salt in a narrower vessel is as blue through the middle of a wider one: only c * L
	// of the salt counts; what differs is the water's own absorption over twice the path, far below sight
	ItemView strong = solution(1.0), weak = solution(0.5);
	OpticsScene a, b;
	lab::optics_compose(a, ROOM, { lab::OpticsBody{ &strong, X, Y, W, H } });
	weak.volume_ml = 2000.0; // twice as wide
	weak.liquid_ml = weak.condensed_ml = 800.0;
	weak.contents = { substance("water", 'l', 44.0, 800.0), substance("copper_sulfate_aq", 'a', 0.4, 63.8) };
	lab::optics_compose(b, ROOM, { lab::OpticsBody{ &weak, X, Y, W, H } });
	CHECK(lab::difference(seen(a, MIDDLE, FLOOR - 5), seen(b, MIDDLE, FLOOR - 5)) < 0.2); // water: 0.07
}

TEST_CASE("optics: a surface and a rim are covered by degrees: T to the power of the coverage (Stage 10)") {
	const ItemView v = solution(1.3);
	const OpticsScene scene = scene_of(v);
	// the surface of 100 mL in this vessel, in pixels from the top
	const double surface = Y + H - 100.0 / (3.14159265358979323846 * DIAMETER_CM * DIAMETER_CM / 4.0) / (DIAMETER_CM / W);
	const int row = static_cast<int>(std::floor(surface));
	const double below = seen(scene, MIDDLE, row + 2).l, above = seen(scene, MIDDLE, row - 2).l;
	const double at = seen(scene, MIDDLE, row).l;
	CHECK(above > at);
	CHECK(at > below);
	CHECK(lab::optics_cover(scene, static_cast<int>(X) - 1, static_cast<int>(Y) + 50) == doctest::Approx(0.0));
	CHECK(lab::optics_cover(scene, static_cast<int>(X), static_cast<int>(Y) + 50) == doctest::Approx(1.0));
	CHECK(lab::optics_cover(scene, static_cast<int>(X) + 3, static_cast<int>(Y) + 50) == 1.0);
}

TEST_CASE("optics: a solid is seen by the room's light, and through what covers it (Stage 10)") {
	const struct {
		const char *species;
		chem::SpeciesId id;
	} solids[] = { { "malachite", chem::builtin::malachite }, { "copper_oxide", chem::builtin::copper_oxide }, { "copper_sulfate_pentahydrate", chem::builtin::copper_sulfate_pentahydrate },
		{ "copper_sulfate", chem::builtin::copper_sulfate } };
	for (const auto &solid : solids) {
		ItemView dry = vessel();
		dry.condensed_ml = 1.25;
		dry.contents = { substance(solid.species, 's', 0.02, 5.0, 1000000, 100.0) };
		const OpticsScene scene = scene_of(dry);
		CHECK(lab::difference(seen(scene, MIDDLE, FLOOR), reference(0.0, &chem::optics::spectrum(solid.id))) <= TRUTH_FIT);
		// a heap: 5 g is a centimetre high in the middle and gone by the wall
		CHECK(lab::difference(seen(scene, MIDDLE, FLOOR - 8), seen(scene, MIDDLE, FLOOR)) < 1e-9);
		CHECK(lab::difference(seen(scene, MIDDLE, FLOOR - 30), reference(0.0)) < 0.01);
		CHECK(lab::difference(seen(scene, static_cast<int>(X) + 5, FLOOR), reference(0.0)) < 0.01);
		// under 100 mL of solution: lit through it and seen through it
		ItemView wet = solution(0.45);
		wet.condensed_ml += 1.25;
		wet.contents.push_back(dry.contents[0]);
		CHECK(lab::difference(seen(scene_of(wet), MIDDLE, FLOOR), reference(0.45 * DIAMETER_CM, &chem::optics::spectrum(solid.id))) <= TRUTH_FIT);
	}
	const lab::CieLab malachite = reference(0.0, &chem::optics::spectrum(chem::builtin::malachite));
	CHECK(malachite.a < -10.0); // green
	CHECK(reference(0.0, &chem::optics::spectrum(chem::builtin::copper_oxide)).l < 25.0); // black
	CHECK(reference(0.0, &chem::optics::spectrum(chem::builtin::copper_sulfate)).l > 90.0); // white
	CHECK(reference(0.0, &chem::optics::spectrum(chem::builtin::copper_sulfate_pentahydrate)).b < -10.0); // blue
}

TEST_CASE("optics: stirred up, a powder clouds the liquid; at rest it lies under it (Stage 10)") {
	ItemView v = solution(0.2);
	v.condensed_ml += 0.6;
	v.contents.push_back(substance("copper_oxide", 's', 0.045, 3.6, 2000000, 80.0));
	const OpticsScene rest = scene_of(v);
	v.stirred = true;
	const OpticsScene stirred = scene_of(v);
	// high in the liquid: clear at rest, dark with the black powder when stirred
	const int y = FLOOR - 25;
	CHECK(lab::difference(seen(rest, MIDDLE, y), reference(0.2 * DIAMETER_CM)) <= TRUTH_FIT);
	CHECK(seen(stirred, MIDDLE, y).l < seen(rest, MIDDLE, y).l - 20.0);
	// on the floor: the heap at rest, the same cloud when stirred
	CHECK(seen(rest, MIDDLE, FLOOR).l < 30.0);
	CHECK(lab::difference(seen(stirred, MIDDLE, FLOOR), seen(stirred, MIDDLE, y)) < 1e-9);
	// a few specks cloud nothing
	v.contents.back() = substance("copper_oxide", 's', 1e-6, 8e-5, 40, 80.0);
	CHECK(lab::difference(seen(scene_of(v), MIDDLE, y), reference(0.2 * DIAMETER_CM)) <= TRUTH_FIT);
}

TEST_CASE("optics: a hot solid glows, as hot as it is (Stage 10)") {
	// what the pixel shows of the room's light, taken away: what glows
	const auto glow_of = [](const OpticsScene &scene, int x, int y, double rgb[3]) {
		double all[3];
		lab::optics_truth(scene, x, y, all);
		// (a solid's glow is a light in the pass: each body's glow at 1 nm, a multiple of the room's light)
		OpticsScene cold = scene;
		for (int i = 0; i < static_cast<int>(cold.table[0]); ++i) {
			for (int k = 0; k < lab::optics_layout::ROW; ++k) {
				cold.rows[static_cast<size_t>((i * lab::optics_layout::ROWS_PER_SET + lab::optics_layout::GLOW_ROW) * lab::optics_layout::ROW + k)] = 0.0f;
			}
		}
		double lit[3];
		lab::optics_truth(cold, x, y, lit);
		for (int c = 0; c < 3; ++c) {
			rgb[c] = all[c] - lit[c];
		}
	};
	const auto glow_at = [&](double celsius, double rgb[3]) {
		ItemView v = vessel();
		v.celsius = celsius;
		v.condensed_ml = 1.0;
		v.contents = { substance("copper_oxide", 's', 0.08, 6.3, 1000000, 100.0) };
		const OpticsScene scene = scene_of(v);
		glow_of(scene, MIDDLE, FLOOR, rgb);
		double above[3];
		glow_of(scene, MIDDLE, FLOOR - 40, above);
		CHECK(above[0] == 0.0); // only the solid
	};
	double cold[3], warm[3], red[3], bright[3];
	glow_at(20.0, cold);
	glow_at(350.0, warm); // the demo's hottest: malachite decomposing
	glow_at(850.0, red);
	glow_at(1300.0, bright);
	CHECK(cold[0] == 0.0);
	CHECK(warm[0] < 1e-6); // nothing an eye in a lit room would see
	// 1123 K: a dull red, about a third as bright as a white page in the room's light
	const double luminance_red = 0.2126 * red[0] + 0.7152 * red[1] + 0.0722 * red[2];
	CHECK(luminance_red > 0.2);
	CHECK(luminance_red < 0.5);
	CHECK(red[0] > 3.0 * red[1]);
	CHECK(red[1] > red[2]);
	CHECK(bright[0] > 30.0 * red[0]); // and far past the display's white
	// a black body at 2042 K (freezing platinum) is 600 000 cd/m2: the old standard of light
	ItemView v = vessel();
	v.celsius = 2042.0 - 273.15;
	v.condensed_ml = 1.0;
	v.contents = { substance("copper_oxide", 's', 0.08, 6.3, 1000000, 100.0) };
	double hot[3];
	glow_of(scene_of(v), MIDDLE, FLOOR, hot);
	const double luminance = (0.2126 * hot[0] + 0.7152 * hot[1] + 0.0722 * hot[2]) * lab::ROOM_WHITE_CD_M2 / (1.0 - 0.035);
	CHECK(luminance == doctest::Approx(600000.0).epsilon(0.03));
}

TEST_CASE("optics: a balloon is its rubber: red, paler as it stretches, deeper at the rim (Stage 10)") {
	const auto balloon = [](double gas_ml, double condensed_ml) {
		ItemView v;
		v.kind = "balloon";
		v.gas_ml = gas_ml;
		v.condensed_ml = v.liquid_ml = condensed_ml;
		SubstanceView rubber = substance("latex", 's', 0.0, 2.0);
		rubber.wall = true;
		v.contents = { rubber };
		if (condensed_ml > 0.0) {
			v.contents.push_back(substance("water", 'l', condensed_ml / 18.0, condensed_ml));
		}
		return v;
	};
	const ItemView limp = balloon(50, 0), full = balloon(800, 0);
	const OpticsScene small = scene_of(limp, 60, 60), large = scene_of(full, 140, 140);
	const int x0 = static_cast<int>(X), y0 = static_cast<int>(Y);
	const lab::CieLab a = seen(small, x0 + 30, y0 + 30), b = seen(large, x0 + 70, y0 + 70);
	CHECK(a.a > 20.0); // red
	CHECK(b.a > 5.0);
	CHECK(b.l > a.l + 10.0);
	CHECK(seen(large, x0 + 8, y0 + 70).l < b.l - 5.0); // the rim
	double rgba[4];
	lab::optics_pixel(large, x0 + 5, y0 + 5, rgba); // outside the circle, inside its rectangle
	CHECK(rgba[3] == 0.0);
	// no solid is in it: the rubber is a wall, not a heap
	CHECK(lab::difference(seen(large, x0 + 70, y0 + 137), seen(large, x0 + 70, y0 + 2)) < 1e-6);
	// what condensed in it lies at the bottom, and water is clear
	const ItemView wet = balloon(800, 5);
	const OpticsScene pool = scene_of(wet, 140, 140);
	CHECK(pool.table[static_cast<size_t>(lab::optics_layout::HEADER_VECS + lab::optics_layout::FORM) * 4 + 2] < Y + 140);
	CHECK(pool.table[static_cast<size_t>(lab::optics_layout::HEADER_VECS + lab::optics_layout::FORM) * 4 + 2] > Y + 120);
}

TEST_CASE("O1, O2: the checks hold on a scene and fire on a corrupted one (Stage 10)") {
	ItemView v = solution(0.5);
	v.condensed_ml += 1.25;
	v.contents.push_back(substance("malachite", 's', 0.02, 5.0, 1000000, 100.0));
	OpticsScene scene = scene_of(v);
	CHECK(lab::check_O1(scene) == "");
	for (int y = static_cast<int>(Y) - 2; y < static_cast<int>(Y + H) + 2; y += 3) {
		for (int x = static_cast<int>(X) - 2; x < static_cast<int>(X + W) + 2; x += 3) {
			REQUIRE(lab::check_O2(scene, x, y) == "");
		}
	}
	using namespace lab::optics_layout;
	const size_t liquid = static_cast<size_t>(LIQUID_ROW * ROW), solid = static_cast<size_t>(SOLID_ROW * ROW);
	{
		OpticsScene bad = scene; // a liquid that makes light
		for (size_t i = 0; i < 20; ++i) {
			bad.rows[liquid + 150 + i] = -0.4f;
		}
		CHECK(lab::check_O1(bad).rfind("O1:", 0) == 0);
		CHECK(lab::check_O2(bad, MIDDLE, FLOOR - 20).rfind("O2:", 0) == 0);
	}
	{
		OpticsScene bad = scene; // a solid that returns more than falls on it
		for (size_t i = 0; i < 401; ++i) {
			bad.rows[solid + i] = 1.3f;
			bad.rows[liquid + i] = 0.0f;
		}
		CHECK(lab::check_O1(bad).rfind("O1:", 0) == 0);
		CHECK(lab::check_O2(bad, MIDDLE, FLOOR).rfind("O2:", 0) == 0);
	}
	{
		OpticsScene bad = scene; // a line with no width
		bad.table[static_cast<size_t>(HEADER_VECS + IDS) * 4 + 3] = 1.0f;
		CHECK(lab::check_O1(bad).rfind("O1:", 0) == 0);
	}
	{
		OpticsScene bad = scene;
		bad.table[0] = static_cast<float>(lab::optics_layout::MAX_BODIES + 1); // more bodies than there is room for
		CHECK(lab::check_O1(bad).rfind("O1:", 0) == 0);
	}
	{
		OpticsScene bad = scene; // a body made of a set of spectra that is not there
		bad.table[static_cast<size_t>(HEADER_VECS + lab::optics_layout::SET) * 4] = 3.0f;
		CHECK(lab::check_O1(bad).rfind("O1:", 0) == 0);
		bad = scene;
		bad.table.resize(8);
		CHECK(lab::check_O1(bad).rfind("O1:", 0) == 0);
	}
}

TEST_CASE("optics: the picture is the pixels, and the table moves only when what is seen does (Stage 10)") {
	const ItemView v = solution(0.5);
	OpticsScene scene = scene_of(v);
	const uint64_t revision = scene.revision;
	lab::optics_compose(scene, ROOM, { lab::OpticsBody{ &v, X, Y, W, H } });
	CHECK(scene.revision == revision);
	lab::optics_compose(scene, ROOM, { lab::OpticsBody{ &v, X + 1, Y, W, H } });
	CHECK(scene.revision == revision + 1);
	const std::vector<uint8_t> picture = lab::optics_picture(scene, 90, 90, 40, 30);
	REQUIRE(picture.size() == 40 * 30 * 4);
	for (const auto &[col, row] : { std::pair{ 0, 0 }, std::pair{ 20, 15 }, std::pair{ 39, 29 } }) {
		double rgba[4];
		lab::optics_pixel(scene, 90 + col, 90 + row, rgba);
		for (int c = 0; c < 4; ++c) {
			CHECK(picture[static_cast<size_t>((row * 40 + col) * 4 + c)] == std::lround(rgba[c] * 255.0));
		}
	}
	// more bodies than the table holds: the rest are left out, none is half written
	std::vector<lab::OpticsBody> many(lab::optics_layout::MAX_BODIES + 44, lab::OpticsBody{ &v, X, Y, W, H });
	lab::optics_compose(scene, ROOM, many);
	CHECK(scene.table[0] == lab::optics_layout::MAX_BODIES);
	CHECK(lab::check_O1(scene) == "");
}

TEST_CASE("optics: through the demo the screen's scene holds O1 and O2, and the flask turns blue (Stage 10)") {
	std::string why;
	const std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/demo.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	const auto flask_colour = [&] {
		float x = 0, y = 0;
		REQUIRE(ui->locate("item:flask", x, y));
		ui->frame(0.0);
		// low in the flask, clear of the heap, over its floor of glass (Stage 21: what it holds rests on that),
		// half way up its liquid (Stage 24: a conical flask's wide base holds what is left in a thin layer)
		const lab::OpticsScene &scene = ui->optics();
		const float *rect = &scene.table[static_cast<size_t>(lab::optics_layout::HEADER_VECS) * 4];
		for (int i = 0; i < static_cast<int>(scene.table[0]); ++i, rect += lab::optics_layout::BODY_VECS * 4) {
			if (std::fabs(rect[0] + rect[2] / 2 - x) < 1 && std::fabs(rect[1] + rect[3] / 2 - y) < 1) {
				const float floor_px = rect[lab::optics_layout::REFRACT * 4 + 2] / rect[lab::optics_layout::FORM * 4 + 1];
				const float floor_top = rect[1] + rect[3] - floor_px, surface = rect[lab::optics_layout::FORM * 4 + 2];
				return seen(scene, x + rect[2] / 4, surface > 0.0f && surface < floor_top - 2.0f ? 0.5f * (surface + floor_top) : floor_top - 4.0f);
			}
		}
		FAIL("the flask is not in the scene");
		return lab::CieLab{};
	};
	const lab::DrawList &list = ui->draw();
	CHECK(list.above > 0);
	CHECK(list.above < static_cast<int32_t>(list.commands.size()));
	CHECK(ui->optics().table[0] == 9); // five vessels, the jar, the balloon; the burner and the condenser's outlet (Stage 22)
	const lab::CieLab empty = flask_colour();
	CHECK(std::fabs(empty.b) < 0.5);

	const nlohmann::json session = nlohmann::json::parse(read("data/sessions/demo.json"));
	lab::CieLab after_dissolving{}, boiled_down{};
	int n = 0;
	for (const nlohmann::json &command : session.at("script")) {
		REQUIRE_MESSAGE(ui->lab().command(command.dump(), why), why);
		ui->frame(0.0);
		const lab::OpticsScene &scene = ui->optics();
		REQUIRE(lab::check_O1(scene) == "");
		// (rows dealt among threads: the reference's scene in use is each thread's own)
		std::vector<std::string> said(12);
		std::vector<std::thread> threads;
		for (size_t t = 0; t < said.size(); ++t) {
			threads.emplace_back([&, t] {
				for (int y = 100 + 17 * static_cast<int>(t); y < 1080 && said[t].empty(); y += 17 * static_cast<int>(said.size())) {
					for (int x = 3; x < 1400 && said[t].empty(); x += 17) {
						said[t] = lab::check_O2(scene, x, y);
					}
				}
			});
		}
		for (std::thread &t : threads) {
			t.join();
		}
		for (const std::string &o2 : said) {
			REQUIRE_MESSAGE(o2.empty(), o2 << " after command " << n);
		}
		if (command.value("do", "") == "stir" && !command.at("on").get<bool>()) {
			after_dissolving = flask_colour();
		}
		if (command.contains("below_g") && command.value("species", "") == "water") {
			boiled_down = flask_colour();
		}
		++n;
	}
	CHECK(after_dissolving.b < -15.0); // blue
	CHECK(boiled_down.b < -15.0);
	CHECK(boiled_down.l < after_dissolving.l - 10.0); // and deeper for the water gone
}

// ---- the renderer's frames (GAME_SPEC §12.2): history kept by content, points by need

namespace {

// the rectangle of the test vessel, and a little around it
constexpr int FX = 95, FY = 95, FW = 100, FH = 125;

// what the renderer shows over the rectangle, every second pixel, against the truth (kept while the scene
// is the same): the pixels visibly wrong now, and the longest run of frames any pixel stayed so
struct Watch {
	int STEP = 2;
	std::vector<int> running = std::vector<int>(static_cast<size_t>(FW * FH), 0);
	std::vector<double> truth = std::vector<double>(static_cast<size_t>(FW * FH) * 3, 0.0);
	uint64_t revision = ~0ull;
	int longest = 0;
	int visible = 0;
	void look(const OpticsScene &scene, const lab::OpticsFrames &frames) {
		visible = 0;
		const bool fresh = scene.revision != revision;
		revision = scene.revision;
		for (int y = FY; y < FY + FH; y += STEP) {
			for (int x = FX; x < FX + FW; x += STEP) {
				const size_t i = static_cast<size_t>((y - FY) * FW + (x - FX));
				int &run = running[i];
				double *t = &truth[i * 3];
				if (fresh) {
					lab::optics_truth(scene, x, y, t);
				}
				double now[3];
				frames.colour(scene, x, y, now);
				if (lab::optics_cover(scene, x, y) > 0.0 && eye_between(t, now) > lab::EYE_VISIBLE) {
					++run;
					++visible;
					longest = std::max(longest, run);
				} else {
					run = 0;
				}
			}
		}
	}
};

} // namespace

TEST_CASE("optics: the renderer's frames settle on each pixel's colour, and then stop spending (Stage 10)") {
	ItemView v = solution(1.3);
	v.condensed_ml += 1.25;
	v.contents.push_back(substance("malachite", 's', 0.02, 5.0, 1000000, 100.0));
	const OpticsScene scene = scene_of(v);
	lab::OpticsFrames frames(FX, FY, FW, FH);
	Watch watch;
	std::vector<double> spent;
	for (int f = 0; f < 30; ++f) {
		frames.frame(scene);
		watch.look(scene, frames);
		spent.push_back(frames.points());
	}
	// the first frame starts every history, 64 points a pixel and the fixed estimate's 8
	CHECK(spent[0] > 70.0 * 0.95 * (W * H * 0.8));
	// settled: nothing visible, and nothing spent
	CHECK(watch.visible == 0);
	CHECK(watch.longest < 6);
	CHECK(spent.back() == 0.0);
	double worst = 0.0, mean = 0.0;
	int covered = 0;
	for (int y = FY; y < FY + FH; ++y) {
		for (int x = FX; x < FX + FW; ++x) {
			if (lab::optics_cover(scene, x, y) > 0.0) {
				const double e = frames_error(scene, frames, x, y);
				worst = std::max(worst, e);
				mean += e;
				++covered;
			}
		}
	}
	MESSAGE("settled after 30 frames: worst " << worst << ", mean " << mean / covered << " on the eye metric; points a frame: " << spent[0] / covered << ", " << spent[1] / covered << ", "
											  << spent[4] / covered << ", then " << spent.back());
	CHECK(mean / covered < 1.0);
	// what is shown is what the colour says, gamut-mapped and encoded
	double rgba[4], rgb[3];
	frames.shown(scene, MIDDLE, FLOOR - 10, rgba);
	frames.colour(scene, MIDDLE, FLOOR - 10, rgb);
	lab::gamut_map(rgb);
	CHECK(rgba[3] == 1.0);
	CHECK(rgba[2] == doctest::Approx(lab::srgb_encode(rgb[2])).epsilon(1e-9));
}

TEST_CASE("optics: a moving body keeps its history where its colour barely moves; a pour starts it again (Stage 10)") {
	ItemView v = solution(0.5);
	OpticsScene scene;
	lab::OpticsFrames frames(FX, FY, FW, FH);
	Watch watch;
	float x = X;
	lab::optics_compose(scene, ROOM, { lab::OpticsBody{ &v, x, Y, W - 10, H } });
	for (int f = 0; f < 20; ++f) {
		frames.frame(scene);
	}
	// dragged a third of a pixel a frame: the inside keeps its history and stays right
	int restarted = 0, inside = 0;
	for (int f = 0; f < 15; ++f) {
		x += 0.33f;
		lab::optics_compose(scene, ROOM, { lab::OpticsBody{ &v, x, Y, W - 10, H } });
		frames.frame(scene);
		watch.look(scene, frames);
		for (int yy = FY + 40; yy < FY + 100; yy += 3) {
			for (int xx = static_cast<int>(x) + 10; xx < static_cast<int>(x + W - 10) - 10; xx += 3) {
				restarted += frames.restarted(xx, yy) ? 1 : 0;
				++inside;
			}
		}
	}
	MESSAGE("dragged: " << restarted << " of " << inside << " inside pixel-frames started again; the longest visible run " << watch.longest << " frames");
	CHECK(restarted < inside / 10);
	CHECK(watch.longest < 6);
	// poured: twice as strong at once; the liquid starts again, and is right within a few frames
	v.contents[1].mol *= 4.0;
	lab::optics_compose(scene, ROOM, { lab::OpticsBody{ &v, x, Y, W - 10, H } });
	frames.frame(scene);
	CHECK(frames.restarted(static_cast<int>(x + (W - 10) / 2), FLOOR - 10));
	for (int f = 0; f < 8; ++f) {
		frames.frame(scene);
		watch.look(scene, frames);
	}
	CHECK(watch.visible == 0);
	CHECK(watch.longest < 6);
}

TEST_CASE("optics: a slow reaction is followed without starting the history again (Stage 10)") {
	ItemView v = solution(0.2);
	OpticsScene scene = scene_of(v);
	lab::OpticsFrames frames(FX, FY, FW, FH);
	Watch watch;
	watch.STEP = 5; // the truth changes every frame: every fifth pixel
	for (int f = 0; f < 12; ++f) {
		frames.frame(scene);
	}
	int restarted = 0;
	for (int f = 0; f < 60; ++f) {
		v.contents[1].mol *= 1.02; // two per cent more salt a frame: 3.3 times over a second
		lab::optics_compose(scene, ROOM, { lab::OpticsBody{ &v, X, Y, W, H } });
		frames.frame(scene);
		watch.look(scene, frames);
		restarted += frames.restarted(MIDDLE, FLOOR - 10) ? 1 : 0;
	}
	MESSAGE("a slow reaction: the middle started again " << restarted << " times in 60 frames; the longest visible run " << watch.longest);
	CHECK(watch.longest < 6);
	CHECK(restarted <= 20);
}

TEST_CASE("optics: the budget is a ceiling on what the frames ask for (Stage 10)") {
	const ItemView v = solution(0.5);
	const OpticsScene scene = scene_of(v);
	lab::OpticsFrames generous(FX, FY, FW, FH, 16.0), mean(FX, FY, FW, FH, 0.5);
	double spent_generous = 0.0, spent_mean = 0.0;
	for (int f = 0; f < 20; ++f) {
		generous.frame(scene);
		mean.frame(scene);
		spent_generous += f > 0 ? generous.points() : 0.0;
		spent_mean += f > 0 ? mean.points() : 0.0;
	}
	// after the first frame (every pixel fresh, before any total is known) what is asked is scaled down to the
	// budget: most pixels draw no batch in a frame (a batch is never under 16 points), and the history fills
	// more slowly, but it fills
	const double pixels = static_cast<double>(FW) * FH;
	MESSAGE("points a pixel a frame over frames 1 to 19: " << spent_generous / pixels / 19 << " at a budget of 16, " << spent_mean / pixels / 19 << " at 0.5");
	CHECK(spent_mean < spent_generous / 2.0);
	CHECK(spent_mean / pixels / 19 < 1.0);
	for (int f = 0; f < 200; ++f) {
		mean.frame(scene);
	}
	CHECK(frames_error(scene, mean, MIDDLE, FLOOR - 10) < lab::EYE_VISIBLE);
}

TEST_CASE("optics: a bench of two hundred bodies: each pixel finds its own through the screen's squares, as if each stood alone (Stage 22)") {
	// what a pixel sees is found among the bodies its square of the screen lists, not among all of them: so with
	// two hundred small vessels on the screen each pixel is what it is with its own vessel alone
	namespace L = lab::optics_layout;
	std::vector<ItemView> items;
	for (int k = 0; k < 200; ++k) {
		ItemView v = solution(0.05 + 0.005 * k);
		v.volume_ml = 50.0;
		items.push_back(v);
	}
	std::vector<lab::OpticsBody> bodies;
	for (int k = 0; k < 200; ++k) {
		bodies.push_back(lab::OpticsBody{ &items[static_cast<size_t>(k)], 20.0f + 92.0f * static_cast<float>(k % 20), 30.0f + 100.0f * static_cast<float>(k / 20), 36.0f, 45.0f });
	}
	OpticsScene all;
	lab::optics_compose(all, ROOM, bodies);
	REQUIRE(lab::check_O1(all) == "");
	CHECK(all.table[0] == 200.0f);
	CHECK(all.rows.size() == static_cast<size_t>(200 * L::ROWS_PER_SET * L::ROW)); // a set of spectra each
	int compared = 0;
	for (int k = 0; k < 200; k += 7) {
		OpticsScene alone;
		lab::optics_compose(alone, ROOM, { bodies[static_cast<size_t>(k)] });
		const lab::OpticsBody &b = bodies[static_cast<size_t>(k)];
		for (const float fx : { 0.1f, 0.5f, 0.85f }) {
			const int x = static_cast<int>(b.x + fx * b.w), y = static_cast<int>(b.y + 0.8f * b.h);
			CAPTURE(k);
			CAPTURE(x);
			CHECK(lab::optics_through(all, x, y, 550.0) == doctest::Approx(lab::optics_through(alone, x, y, 550.0)).epsilon(1e-9));
			++compared;
		}
	}
	CHECK(compared > 80);
	// a square lists the bodies that reach into it, the nearest the eye kept where more than seven do
	std::vector<lab::OpticsBody> heap(10, lab::OpticsBody{ &items[0], 400.0f, 400.0f, 36.0f, 45.0f });
	OpticsScene crowded;
	lab::optics_compose(crowded, ROOM, heap);
	const float *square = &crowded.table[static_cast<size_t>(L::GRID_BASE + 2 * (10 * L::GRID_COLS + 10)) * 4];
	CHECK(square[0] == 7.0f);
	CHECK(square[1] == 3.0f); // the three furthest are left out
	CHECK(square[7] == 9.0f); // the nearest, last
	// past the screen's edge a body is listed in the edge's squares, and its pixels found there
	OpticsScene edge;
	lab::optics_compose(edge, ROOM, { lab::OpticsBody{ &items[3], 1900.0f, 1050.0f, 36.0f, 45.0f } });
	CHECK(lab::optics_cover(edge, 1910, 1070) > 0.0);
}
