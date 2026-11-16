// The optical pass (GAME_SPEC §12): the table the renderer is given, and the
// reference renderer, which is the shader's own source compiled as C++.
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

using lab::ItemView;
using lab::OpticsScene;
using lab::SubstanceView;

namespace {

constexpr double BACKDROP = 0.7305;
const lab::OpticsRoom ROOM{ 0, 0, 1000, 1000, BACKDROP, 0.02 };
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

lab::CieLab seen(const OpticsScene &scene, double x, double y) {
	const lab::PixelLight l = lab::optics_transport(scene, x, y);
	return lab::to_lab(chem::optics::colour(chem::optics::game_bands(), l.band));
}

// the 1 nm colour of the backdrop behind `absorbance` of the dissolved salt, or of a solid under it
lab::CieLab reference(double absorbance_scale, const chem::optics::Spectrum *reflectance = nullptr) {
	const chem::optics::Spectrum &salt = chem::optics::spectrum(chem::builtin::copper_sulfate_aq);
	chem::optics::Spectrum factor;
	for (size_t i = 0; i < factor.size(); ++i) {
		factor[i] = (reflectance != nullptr ? (*reflectance)[i] : BACKDROP) * std::pow(10.0, -salt[i] * absorbance_scale);
	}
	return lab::to_lab(chem::optics::colour(factor));
}

const double FLOOR = Y + H - 1; // the row of pixels on the vessel's floor
const double MIDDLE = X + W / 2 - 0.5;

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
		CHECK(lab::difference(middle, reference(molar * DIAMETER_CM)) <= lab::BAND_ERROR_WORST);
		// a quarter of the way in from the rim the chord is sqrt(3)/2 of it
		const double x = X + W / 4 - 0.5;
		CHECK(lab::difference(seen(scene, x, FLOOR - 10), reference(molar * DIAMETER_CM * std::sqrt(3.0) / 2.0)) <= lab::BAND_ERROR_WORST);
		// above the surface, nothing
		CHECK(lab::difference(seen(scene, MIDDLE, Y + 10), reference(0.0)) < 1e-4);
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
	// the same amount of salt in a narrower vessel is as blue through the middle of a wider one: only c * L counts
	ItemView strong = solution(1.0), weak = solution(0.5);
	OpticsScene a, b;
	lab::optics_compose(a, ROOM, { lab::OpticsBody{ &strong, X, Y, W, H } });
	weak.volume_ml = 2000.0; // twice as wide
	weak.liquid_ml = weak.condensed_ml = 800.0;
	weak.contents = { substance("water", 'l', 44.0, 800.0), substance("copper_sulfate_aq", 'a', 0.4, 63.8) };
	lab::optics_compose(b, ROOM, { lab::OpticsBody{ &weak, X, Y, W, H } });
	CHECK(lab::difference(seen(a, MIDDLE, FLOOR - 5), seen(b, MIDDLE, FLOOR - 5)) < 1e-3);
}

TEST_CASE("optics: a surface and a rim are covered by degrees: T to the power of the coverage (Stage 10)") {
	const ItemView v = solution(1.3);
	const OpticsScene scene = scene_of(v);
	// the surface of 100 mL in this vessel, in pixels from the top
	const double surface = Y + H - 100.0 / (3.14159265358979323846 * DIAMETER_CM * DIAMETER_CM / 4.0) / (DIAMETER_CM / W);
	const double below = seen(scene, MIDDLE, std::floor(surface) + 2).l, above = seen(scene, MIDDLE, std::floor(surface) - 2).l;
	const double at = seen(scene, MIDDLE, std::floor(surface)).l;
	CHECK(above > at);
	CHECK(at > below);
	CHECK(lab::optics_transport(scene, X - 1, Y + 50).cover == doctest::Approx(0.0));
	CHECK(lab::optics_transport(scene, X, Y + 50).cover == doctest::Approx(1.0));
	CHECK(lab::optics_transport(scene, X + 3, Y + 50).cover == 1.0);
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
		CHECK(lab::difference(seen(scene, MIDDLE, FLOOR), reference(0.0, &chem::optics::spectrum(solid.id))) <= lab::BAND_ERROR_WORST);
		// a heap: 5 g is a centimetre high in the middle and gone by the wall
		CHECK(lab::difference(seen(scene, MIDDLE, FLOOR - 8), seen(scene, MIDDLE, FLOOR)) < 1e-9);
		CHECK(lab::difference(seen(scene, MIDDLE, FLOOR - 30), reference(0.0)) < 1e-4);
		CHECK(lab::difference(seen(scene, X + 5, FLOOR), reference(0.0)) < 1e-4);
		// under 100 mL of solution: lit through it and seen through it
		ItemView wet = solution(0.45);
		wet.condensed_ml += 1.25;
		wet.contents.push_back(dry.contents[0]);
		CHECK(lab::difference(seen(scene_of(wet), MIDDLE, FLOOR), reference(0.45 * DIAMETER_CM, &chem::optics::spectrum(solid.id))) <= lab::BAND_ERROR_WORST);
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
	const double y = FLOOR - 25;
	CHECK(lab::difference(seen(rest, MIDDLE, y), reference(0.2 * DIAMETER_CM)) <= lab::BAND_ERROR_WORST);
	CHECK(seen(stirred, MIDDLE, y).l < seen(rest, MIDDLE, y).l - 20.0);
	// on the floor: the heap at rest, the same cloud when stirred
	CHECK(seen(rest, MIDDLE, FLOOR).l < 30.0);
	CHECK(lab::difference(seen(stirred, MIDDLE, FLOOR), seen(stirred, MIDDLE, y)) < 1e-9);
	// a few specks cloud nothing
	v.contents.back() = substance("copper_oxide", 's', 1e-6, 8e-5, 40, 80.0);
	CHECK(lab::difference(seen(scene_of(v), MIDDLE, y), reference(0.2 * DIAMETER_CM)) <= lab::BAND_ERROR_WORST);
}

TEST_CASE("optics: a hot solid glows, as hot as it is (Stage 10)") {
	const auto glow_at = [](double celsius, double rgb[3]) {
		ItemView v = vessel();
		v.celsius = celsius;
		v.condensed_ml = 1.0;
		v.contents = { substance("copper_oxide", 's', 0.08, 6.3, 1000000, 100.0) };
		const lab::PixelLight l = lab::optics_transport(scene_of(v), MIDDLE, FLOOR);
		for (int c = 0; c < 3; ++c) {
			rgb[c] = l.glow[c];
		}
		CHECK(lab::optics_transport(scene_of(v), MIDDLE, FLOOR - 40).glow[0] == 0.0); // only the solid
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
	const lab::PixelLight l = lab::optics_transport(scene_of(v), MIDDLE, FLOOR);
	const double luminance = (0.2126 * l.glow[0] + 0.7152 * l.glow[1] + 0.0722 * l.glow[2]) * lab::ROOM_WHITE_CD_M2 / (1.0 - 0.035);
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
	const lab::CieLab a = seen(small, X + 30, Y + 30), b = seen(large, X + 70, Y + 70);
	CHECK(a.a > 20.0); // red
	CHECK(b.a > 5.0);
	CHECK(b.l > a.l + 10.0);
	CHECK(seen(large, X + 8, Y + 70).l < b.l - 5.0); // the rim
	double rgba[4];
	lab::optics_pixel(large, X + 5, Y + 5, rgba); // outside the circle, inside its rectangle
	CHECK(rgba[3] == 0.0);
	// no solid is in it: the rubber is a wall, not a heap
	CHECK(lab::difference(seen(large, X + 70, Y + 137), seen(large, X + 70, Y + 2)) < 1e-6);
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
	for (double y = Y - 2; y < Y + H + 2; y += 3) {
		for (double x = X - 2; x < X + W + 2; x += 3) {
			REQUIRE(lab::check_O2(scene, x, y) == "");
		}
	}
	using namespace lab::optics_layout;
	const size_t body = static_cast<size_t>(HEADER_VECS) * 4;
	{
		OpticsScene bad = scene; // a liquid that makes light
		bad.table[body + static_cast<size_t>(LIQUID) * 4 + 5] = -0.4f;
		CHECK(lab::check_O1(bad).rfind("O1:", 0) == 0);
		CHECK(lab::check_O2(bad, MIDDLE, FLOOR - 20).rfind("O2:", 0) == 0);
	}
	{
		OpticsScene bad = scene; // a solid that returns more than falls on it
		bad.table[body + static_cast<size_t>(SOLID) * 4 + 2] = 1.3f;
		CHECK(lab::check_O1(bad).rfind("O1:", 0) == 0);
		bad.table[body + static_cast<size_t>(LIQUID) * 4 + 2] = 0.0f;
		CHECK(lab::check_O2(bad, MIDDLE, FLOOR).rfind("O2:", 0) == 0);
	}
	{
		OpticsScene bad = scene;
		bad.table[0] = 40.0f; // more bodies than there is room for
		CHECK(lab::check_O1(bad).rfind("O1:", 0) == 0);
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
	std::vector<lab::OpticsBody> many(40, lab::OpticsBody{ &v, X, Y, W, H });
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
		// low in the flask, clear of the heap
		const lab::OpticsScene &scene = ui->optics();
		const float *rect = &scene.table[static_cast<size_t>(lab::optics_layout::HEADER_VECS) * 4];
		for (int i = 0; i < static_cast<int>(scene.table[0]); ++i, rect += lab::optics_layout::BODY_VECS * 4) {
			if (std::fabs(rect[0] + rect[2] / 2 - x) < 1 && std::fabs(rect[1] + rect[3] / 2 - y) < 1) {
				return seen(scene, x + rect[2] / 4, rect[1] + rect[3] - 4);
			}
		}
		FAIL("the flask is not in the scene");
		return lab::CieLab{};
	};
	const lab::DrawList &list = ui->draw();
	CHECK(list.above > 0);
	CHECK(list.above < static_cast<int32_t>(list.commands.size()));
	CHECK(ui->optics().table[0] == 7); // five vessels, the jar, the balloon
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
		for (double y = 100; y < 1080; y += 7) {
			for (double x = 3; x < 1400; x += 7) {
				const std::string o2 = lab::check_O2(scene, x, y);
				REQUIRE_MESSAGE(o2.empty(), o2 << " after command " << n);
			}
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
