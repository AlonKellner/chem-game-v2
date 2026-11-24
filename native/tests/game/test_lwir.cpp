// The long-wave range (GAME_SPEC §12.4, Stage 46): the renderer's second range of the spectrum, 8-14 um, what a
// thermal camera sees. Every body emits by its temperature (Kirchhoff); in a room at one temperature every point
// reads the room's light (O21); a ray's shares are whole (O22); the light's table is its integral (O23); and the
// visible picture is as it was (O24).
#include "lab/optics_scene.hpp"

#include "chem/optics.hpp"

#include "sparse.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using lab::ItemView;
using lab::OpticsBody;
using lab::OpticsScene;
using lab::SubstanceView;
namespace L = lab::optics_layout;

namespace {

constexpr double ROOM_K = 293.15;

lab::OpticsRoom room(int range) {
	lab::OpticsRoom r;
	r.w = r.h = 1000;
	r.bench = 0.7305;
	r.elsewhere = 0.02;
	r.kelvin = ROOM_K;
	r.range = range;
	for (int k = 0; k < 12; ++k) { // something drawn behind: it is the visible's, not the long-wave's
		r.shapes.push_back(lab::OpticsShape{ 101.3f + 37.7f * static_cast<float>(k), 0.0f, 3.0f, 1000.0f, 0.15 + 0.05 * (k % 5) });
	}
	return r;
}

SubstanceView substance(const char *species, char phase, double mol, double g, double ml, bool wall = false) {
	SubstanceView s;
	s.species = species;
	s.phase = phase;
	s.mol = mol;
	s.g = g;
	s.ml = ml;
	s.wall = wall;
	return s;
}

// a glass vessel of `volume` mL holding `liquid` mL of water at `celsius`, and a heap of copper oxide if `oxide`
ItemView vessel(const char *id, double volume, double liquid, double celsius, bool oxide = false) {
	ItemView v;
	v.id = id;
	v.kind = "vessel";
	v.volume_ml = volume;
	v.celsius = celsius;
	v.liquid_ml = v.condensed_ml = liquid;
	if (liquid > 0.0) {
		v.contents.push_back(substance("water", 'l', liquid / 18.0, liquid, liquid));
	}
	if (oxide) {
		SubstanceView s = substance("copper_oxide", 's', 0.08, 6.3, 1.0);
		s.particles = 1000000;
		s.size_um = 100.0;
		v.contents.push_back(s);
		v.solid_ml = 1.0;
		v.condensed_ml += 1.0;
	}
	v.contents.push_back(substance("glass", 's', 0.83, 50.0, 22.0, true));
	v.wall_mm = 1.1;
	v.floor_mm = 2.6;
	return v;
}

ItemView balloon(double celsius) {
	ItemView v;
	v.id = "balloon";
	v.kind = "balloon";
	v.celsius = celsius;
	v.gas_ml = 400.0;
	v.contents.push_back(substance("latex", 's', 0.01, 2.0, 2.0, true));
	return v;
}

OpticsBody thing(OpticsBody::Shape shape, float x, float y, float w, float h, const char *surface, double kelvin = 0.0) {
	OpticsBody b;
	b.shape = shape;
	b.x = x;
	b.y = y;
	b.w = w;
	b.h = h;
	b.size = 3.0f;
	b.surface = surface;
	b.kelvin = kelvin;
	return b;
}

OpticsBody flame(float x, float y, double kelvin) {
	OpticsBody b = thing(OpticsBody::Cone, x, y, 24, 40, nullptr, kelvin);
	b.light = "flame_sooty";
	b.light_to = "flame_blue";
	b.toward = 0.7;
	return b;
}

// a bench of everything there is, all at `celsius`: a vessel of water, one with a heap, a balloon, a part's face,
// a wire, a flame, the burner's metal; and a held vessel in front
struct Bench {
	ItemView water, heap, held, ball;
	std::vector<OpticsBody> bodies(double celsius) {
		water = vessel("water", 250.0, 100.0, celsius);
		heap = vessel("heap", 250.0, 0.0, celsius, true);
		held = vessel("held", 50.0, 30.0, celsius);
		ball = balloon(celsius);
		const double kelvin = celsius + 273.15;
		std::vector<OpticsBody> b{ OpticsBody{ &water, 200, 100, 90, 112.5f }, OpticsBody{ &heap, 320, 100, 90, 112.5f }, OpticsBody{ &ball, 440, 100, 80, 90 } };
		b.push_back(thing(OpticsBody::Slab, 560, 100, 60, 60, "part_face", kelvin));
		OpticsBody wire = thing(OpticsBody::Bar, 0, 0, 0, 0, "wire", kelvin);
		wire.x0 = 560;
		wire.y0 = 180;
		wire.x1 = 640;
		wire.y1 = 200;
		b.push_back(wire);
		b.push_back(thing(OpticsBody::Slab, 650, 160, 40, 50, "burner_metal", kelvin));
		b.push_back(flame(658, 118, kelvin));
		OpticsBody in_hand{ &held, 225, 140, 70, 87.5f };
		in_hand.layer = 1;
		b.push_back(in_hand);
		return b;
	}
};

OpticsScene compose(const lab::OpticsRoom &r, const std::vector<OpticsBody> &bodies) {
	OpticsScene scene;
	lab::optics_compose(scene, r, bodies);
	REQUIRE(lab::check_O1(scene) == "");
	return scene;
}

// the camera's temperature of a pixel's long-wave light at nm (a black body's that gives it)
double band_kelvin(const OpticsScene &scene, int x, int y) {
	double sum = 0.0;
	const int points = 24;
	for (int k = 0; k < points; ++k) {
		const double nm = chem::optics::LWIR_FIRST_NM + 6000.0 * (k + 0.5) / points;
		sum += lab::optics_through(scene, x, y, nm) * chem::optics::planck(nm, ROOM_K);
	}
	double room = 0.0;
	for (int k = 0; k < points; ++k) {
		room += chem::optics::planck(chem::optics::LWIR_FIRST_NM + 6000.0 * (k + 0.5) / points, ROOM_K);
	}
	return chem::optics::brightness_kelvin(sum / room * chem::optics::band_signal(ROOM_K), 1.0, ROOM_K);
}

} // namespace

TEST_CASE("long-wave: in a room at one temperature every pixel reads the room's light, through anything (O21, Stage 46)") {
	Bench bench;
	const OpticsScene scene = compose(room(1), bench.bodies(ROOM_K - 273.15));
	const int step = sparse() ? 7 : 3;
	CHECK(lab::check_O21(scene, 190, 90, 700, 240, step, 5, 1e-12) == "");
	// it fires: a body a millikelvin off the room (glass, a heap, a flame that emits as if it were not there)
	OpticsScene off = scene;
	off.table[static_cast<size_t>(L::HEADER_VECS + 0 * L::BODY_VECS + L::KELVIN_VEC) * 4 + L::KELVIN_PART] += 0.001f;
	CHECK(lab::check_O21(off, 200, 100, 290, 212, 2, 3, 1e-12) != "");
	OpticsScene cold = scene;
	for (int b = 0; b < static_cast<int>(cold.table[0]); ++b) {
		cold.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS + L::KELVIN_VEC) * 4 + L::KELVIN_PART] = 1.0f; // nothing emits
	}
	CHECK(lab::check_O21(cold, 190, 90, 700, 240, 9, 3, 1e-12) != "");
	// and it holds only in the long-wave range
	CHECK(lab::check_O21(compose(room(0), bench.bodies(ROOM_K - 273.15)), 190, 90, 700, 240, 9, 3, 1e-12) != "");
}

TEST_CASE("long-wave: a ray's shares through absorbing glass are whole: passed, taken in, turned away (O22, Stage 46)") {
	Bench bench;
	const OpticsScene scene = compose(room(1), bench.bodies(20.0));
	int traced = 0;
	for (const bool truth : { false, true }) {
		for (int y = 110; y < 210; y += 7) {
			for (int x = 200; x < 290; x += 3) {
				for (const double nm : { 8200.0, 9100.0, 10600.0, 13300.0, 550.0 }) {
					double shares[3];
					if (!lab::optics_ray_shares(scene, 0, x + 0.37, y + 0.53, nm, truth, shares)) {
						continue;
					}
					++traced;
					CAPTURE(x);
					CAPTURE(y);
					CAPTURE(nm);
					CHECK(lab::check_O22(shares[0], shares[1], shares[2], 1e-12) == "");
					if (nm > 7000.0) {
						CHECK(shares[0] < 1e-9); // glass is opaque in the long-wave
					} else {
						CHECK(shares[1] == 0.0); // and takes in nothing in the visible
					}
				}
			}
		}
	}
	CHECK(traced > 400);
	// it fires: what is not passed taken as turned away, through glass that absorbs
	CHECK(lab::check_O22(0.0, 0.96, 1.0, 1e-12) != "");
	CHECK(lab::check_O22(0.5, 0.0, 0.4, 1e-12) != "");
}

TEST_CASE("long-wave: the light's table is the band's integral, and a temperature comes back from its signal (O23, Stage 46)") {
	for (const double kelvin : { 280.0, 293.15, 305.0 }) {
		CAPTURE(kelvin);
		CHECK(lab::check_O23(chem::optics::lwir_energy_table(kelvin), kelvin) == "");
		// it fires: a table a kelvin off
		CHECK(lab::check_O23(chem::optics::lwir_energy_table(kelvin + 1.0), kelvin) != "");
	}
}

TEST_CASE("long-wave: the visible picture is as it was with the long-wave range there, bodies below 500 K (O24, Stage 46)") {
	Bench bench;
	const std::vector<OpticsBody> bodies = bench.bodies(20.0);
	const OpticsScene with = compose(room(0), bodies);
	// without: its long-wave rows and light another's, and every body at another temperature below 500 K
	OpticsScene without = with;
	const size_t base = static_cast<size_t>(with.table[3]);
	for (size_t i = base; i < without.rows.size(); ++i) {
		without.rows[i] = static_cast<float>(0.25 + 0.5 * std::sin(static_cast<double>(i)) * std::sin(static_cast<double>(i)));
	}
	for (size_t i = static_cast<size_t>(L::LIGHT_SIZE) * 4; i < without.lights.size(); ++i) {
		without.lights[i] = static_cast<float>(i % 97);
	}
	for (int b = 0; b < static_cast<int>(without.table[0]); ++b) {
		without.table[static_cast<size_t>(L::HEADER_VECS + b * L::BODY_VECS + L::KELVIN_VEC) * 4 + L::KELVIN_PART] = static_cast<float>(1.0 + 37.0 * b);
	}
	without.table[4 * 4 + 2] = 451.0f;
	const int step = sparse() ? 9 : 3;
	CHECK(lab::check_O24(with, without, 190, 90, 700, 240, step) == "");
	// it fires: the heap at 900 K glows
	OpticsScene hot = without;
	hot.table[static_cast<size_t>(L::HEADER_VECS + 1 * L::BODY_VECS + L::KELVIN_VEC) * 4 + L::KELVIN_PART] = 900.0f;
	CHECK(lab::check_O24(with, hot, 320, 180, 410, 212, 2) != "");
}

TEST_CASE("long-wave: a hot solid's Kirchhoff glow is the glow row it was, Planck's law times its emissivity (Stage 46)") {
	// a heap of copper oxide at 850 C, seen straight (no refraction): what it adds at each wavelength over the same
	// heap at the room's temperature is Planck's law times 1 - its reflectance, over the room's light, where it covers
	ItemView hot = vessel("hot", 250.0, 0.0, 850.0, true), cold = vessel("hot", 250.0, 0.0, 20.0, true);
	lab::OpticsRoom flat = room(0);
	flat.refraction = false;
	const OpticsScene h = compose(flat, { OpticsBody{ &hot, 200, 100, 90, 112.5f } }), c = compose(flat, { OpticsBody{ &cold, 200, 100, 90, 112.5f } });
	const chem::optics::Spectrum *r = nullptr;
	const chem::SpeciesId oxide = chem::builtin_species().find("copper_oxide");
	r = &chem::optics::spectrum(oxide);
	double white = 0.0;
	for (size_t i = 0; i < chem::optics::d65().size(); ++i) {
		white += chem::optics::ybar()[i] * chem::optics::d65()[i];
	}
	const auto old_glow = [&](int nm) {
		return chem::optics::planck(nm, 1123.15) * (1.0 - (*r)[static_cast<size_t>(nm - 380)]) * 683.002 / lab::ROOM_WHITE_CD_M2 / (chem::optics::d65()[static_cast<size_t>(nm - 380)] / white);
	};
	const auto added = [&](int x, int y, int nm) { return lab::optics_through(h, x, y, nm) - lab::optics_through(c, x, y, nm); };
	// the heap's pixels: what each adds is the glow row times how much of the pixel the heap covers, at every wavelength
	int whole = 0, parts = 0;
	for (int y = 150; y < 213; ++y) {
		const double share = added(245, y, 700) / old_glow(700);
		if (!(share > 1e-6)) {
			continue;
		}
		++parts;
		whole += share > 1.0 - 1e-5 ? 1 : 0;
		CAPTURE(y);
		CHECK(share <= 1.0 + 1e-5); // (the scene's rows are floats, as the glow row was)
		for (const int nm : { 450, 550, 620, 780 }) {
			CAPTURE(nm);
			CHECK(added(245, y, nm) == doctest::Approx(share * old_glow(nm)).epsilon(1e-5));
		}
	}
	CHECK(whole > 2); // the heap covers whole pixels
	CHECK(parts > whole); // and part of its edge's
}

TEST_CASE("long-wave: glass is opaque: a vessel shows its own glass's temperature, not what stands behind it (Stage 46)") {
	// a vessel of water at 70 C before a slab at 200 C: through its middle the camera sees its glass, which emits at
	// 70 C and turns the room back where it does not; the slab behind it is not seen, however hot
	ItemView warm = vessel("warm", 250.0, 100.0, 70.0);
	const auto scene_with = [&](double slab_kelvin) {
		return compose(room(1), { thing(OpticsBody::Slab, 150, 80, 200, 160, "part_face", slab_kelvin), OpticsBody{ &warm, 200, 100, 90, 112.5f } });
	};
	const OpticsScene a = scene_with(473.15), b = scene_with(373.15);
	const double seen = band_kelvin(a, 245, 170);
	MESSAGE("a vessel at 70 C reads " << seen - 273.15 << " C at an emissivity of 1");
	CHECK(seen > ROOM_K + 30.0); // well above the room's
	CHECK(seen < 343.15); // and under its own: glass reflects a little of the room
	CHECK(std::fabs(band_kelvin(b, 245, 170) - seen) < 1e-6); // the slab behind it is not seen
	CHECK(band_kelvin(a, 160, 90) > 460.0); // beside it, the slab is
}

TEST_CASE("long-wave: a flame is faint and a hot metal reads low (Stage 46)") {
	const OpticsScene flames = compose(room(1), { flame(400, 100, 2200.0) });
	const double flame_k = band_kelvin(flames, 412, 135);
	MESSAGE("the burner's flame at 1927 C reads " << flame_k - 273.15 << " C at an emissivity of 1");
	CHECK(flame_k > ROOM_K + 20.0); // seen
	CHECK(flame_k < ROOM_K + 400.0); // and far under its own temperature: a thin gas
	const OpticsScene metal = compose(room(1), { thing(OpticsBody::Slab, 100, 100, 60, 60, "burner_metal", 473.15), thing(OpticsBody::Slab, 200, 100, 60, 60, "part_face", 473.15) });
	const double steel = band_kelvin(metal, 130, 130), plastic = band_kelvin(metal, 230, 130);
	MESSAGE("at 200 C, oxidised steel reads " << steel - 273.15 << " C, a plastic face " << plastic - 273.15 << " C");
	CHECK(steel < plastic - 10.0);
	CHECK(plastic < 473.15);
}

TEST_CASE("long-wave: one scene is the whole room at any view: a point off the screen finds what it does at 1x (Stage 46)") {
	// the bench laid out, and the same seen at 4x with the layout's (0, 0) far to the left and above: its bodies off
	// the screen are still in the scene, and a point of the room reads there what it reads laid out
	Bench bench;
	std::vector<OpticsBody> laid = bench.bodies(20.0);
	laid[3].kelvin = 373.15; // a hot face, a warm wire, the flame at its own
	laid[4].kelvin = 330.0;
	laid[6].kelvin = 2200.0;
	for (int k = 0; k < 26; ++k) {
		// and a row of small slabs, each at its own temperature: more than a square of the screen holds, should those
		// off the screen be heaped on its edge
		laid.push_back(thing(OpticsBody::Slab, 442.0f + 10.0f * static_cast<float>(k), 218, 7, 18, "part_face", 300.0 + 9.0 * k));
	}
	lab::OpticsRoom whole = room(1), zoomed = room(1);
	const double scale = 4.0, ox = -2600.0, oy = -150.0;
	zoomed.pixel = scale;
	zoomed.pixel_x0 = ox;
	zoomed.pixel_y0 = oy;
	zoomed.wall_px *= scale;
	zoomed.gap_px *= scale;
	zoomed.shapes.clear();
	whole.shapes.clear();
	std::vector<OpticsBody> seen = laid;
	for (OpticsBody &b : seen) {
		b.x = static_cast<float>(ox + scale * b.x);
		b.y = static_cast<float>(oy + scale * b.y);
		b.w = static_cast<float>(scale * b.w);
		b.h = static_cast<float>(scale * b.h);
		b.x0 = static_cast<float>(ox + scale * b.x0);
		b.y0 = static_cast<float>(oy + scale * b.y0);
		b.x1 = static_cast<float>(ox + scale * b.x1);
		b.y1 = static_cast<float>(oy + scale * b.y1);
		b.size = static_cast<float>(scale * b.size);
	}
	const OpticsScene a = compose(whole, laid), b = compose(zoomed, seen);
	int off_screen = 0, read = 0;
	for (int y = 102; y < 238; y += 4) {
		for (int x = 442; x < 700; x += 3) {
			const double px = x + 0.47, py = y + 0.47, zx = ox + scale * px, zy = oy + scale * py;
			off_screen += zx < 0.0 || zx > 1920.0 || zy < 0.0 || zy > 1080.0 ? 1 : 0;
			for (const double nm : { 8300.0, 10400.0, 13100.0 }) {
				const double at_1x = lab::optics_point(a, px, py, nm), at_view = lab::optics_point(b, zx, zy, nm);
				CAPTURE(x);
				CAPTURE(y);
				CAPTURE(nm);
				CHECK(at_view == doctest::Approx(at_1x).epsilon(1e-5));
				++read;
			}
		}
	}
	CHECK(off_screen > 100);
	CHECK(read > 1000);
}

namespace {

// a grey ramp, black to white (linear), for a display under test
lab::OpticsRelay grey_relay(float frame_x, float frame_y) {
	lab::OpticsRelay r;
	r.frame_x = frame_x;
	r.frame_y = frame_y;
	r.low_kelvin = 290.0;
	r.high_kelvin = 380.0;
	for (int k = 0; k < 256; ++k) {
		const float v = static_cast<float>(k) / 255.0f;
		r.palette[static_cast<size_t>(k)] = { v, v, v };
	}
	r.bit = 1;
	r.hash = 7;
	return r;
}

// a camera: its case (hidden from its own rays) and its display; and what it looks at: a warm vessel, a hot face
struct Camera {
	ItemView warm = vessel("warm", 250.0, 100.0, 60.0);
	lab::OpticsRelay relay = grey_relay(180.0f, 140.0f);
	double face_kelvin = 343.15; // (within the palette's span)
	std::vector<OpticsBody> bodies() const {
		OpticsBody case_body = thing(OpticsBody::Slab, 96, 296, 48, 38, "viewer_case", 0.0);
		case_body.hidden = 1;
		OpticsBody display = thing(OpticsBody::Sensor, 100, 300, 40, 30, "lcd_black", 0.0);
		display.size = 1.0f;
		display.hidden = 1;
		display.relay = &relay;
		return { thing(OpticsBody::Slab, 150, 110, 60, 60, "part_face", face_kelvin), OpticsBody{ &warm, 200, 100, 90, 112.5f }, case_body, display };
	}
};

} // namespace

TEST_CASE("long-wave: a sensor's display shows its frame's long-wave light, settled to its truth (O25, Stage 46)") {
	Camera cam;
	const std::vector<OpticsBody> bodies = cam.bodies();
	const OpticsScene scene = compose(room(0), bodies);
	CHECK(scene.relay_pool == 40 * 30);
	lab::OpticsFrames frames(100, 300, 40, 30);
	for (int f = 0; f < 60; ++f) {
		frames.frame(scene);
	}
	CHECK(frames.asked() == 0.0); // settled
	const int step = sparse() ? 3 : 1;
	CHECK(lab::check_O25(scene, frames, 100, 300, 140, 330, step) == "");
	// what it shows: the face's left edge, the vessel's glass at 60 C, the room between
	double rgba[4];
	frames.shown(scene, 105, 305, rgba);
	const double face = rgba[0];
	frames.shown(scene, 136, 315, rgba);
	const double glass = rgba[0];
	MESSAGE("the display shows the hot face at " << face << ", the vessel's glass at " << glass);
	CHECK(face > glass);
	// it fires: the frame a pixel off (what the display shows is of the frame's place, pixel for pixel)
	Camera moved;
	moved.relay.frame_x += 1.0f;
	const OpticsScene off = compose(room(0), moved.bodies());
	CHECK(lab::check_O25(off, frames, 100, 300, 140, 330, 1) != "");
	// and what lies beyond is in its key: the face hotter, the display reads it again and settles to it
	Camera hotter;
	hotter.face_kelvin = 368.15;
	const OpticsScene hot = compose(room(0), hotter.bodies());
	CHECK(lab::check_O25(hot, frames, 100, 300, 140, 330, 1) != "");
	for (int f = 0; f < 60; ++f) {
		frames.frame(hot);
	}
	CHECK(lab::check_O25(hot, frames, 100, 300, 140, 330, step) == "");
}
