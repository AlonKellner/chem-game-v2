// The renderer's table, from the state (GAME_SPEC §12).
#include "lab/optics_scene.hpp"
#include "lab/bubbles.hpp"
#include "lab/glassware.hpp"

#include "chem/optics.hpp"
#include "chem/species.hpp"
#include "lab/colour.hpp"
#include "lab/contents.hpp"
#include "lab/optics_trace.hpp"
#include "lab/walls.hpp"

#include <algorithm>
#include <atomic>
#include <array>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <map>
#include <string>
#include <unordered_map>

namespace lab {

namespace {

// a vessel's wall as rings (Stage 53: lab/walls.hpp), cut once for each shape
const std::vector<lab::walls::Ring> &rings_cut(const lab::glass::Vessel *made) {
	static thread_local std::map<const lab::glass::Vessel *, std::vector<lab::walls::Ring>> cut;
	auto found = cut.find(made);
	if (found == cut.end()) {
		found = cut.emplace(made, made != nullptr ? lab::walls::rings_of(*made) : std::vector<lab::walls::Ring>{}).first;
	}
	return found->second;
}

// A revision names what it is of across every scene (Stage 47): a host that shows one scene and then another (the
// lab, then the prism) uploads what changed by its revision, and two scenes' counts from 0 would name different
// tables alike.
uint64_t next_revision() {
	static std::atomic<uint64_t> last{ 0 };
	return ++last;
}

using namespace optics_layout;
constexpr double FIRST_NM_D = chem::optics::FIRST_NM;
using chem::optics::Kind;
using chem::optics::Spectrum;

constexpr double PI = 3.14159265358979323846;
constexpr double LN10 = 2.302585092994046;
constexpr double REPOSE = 0.7; // a heap's slope: tan 35 degrees, a dry powder's angle of repose
constexpr double PACKING = 0.5; // how much of a heap is solid
constexpr double EXTINCTION_EFFICIENCY = 2.0; // of a particle much larger than the wavelength
constexpr double GREY = 0.5; // the reflectance of a solid with no spectrum

// a small whole number for a text or a body's contents: equal when they are, and exact as a float
float tag(uint64_t h) {
	return static_cast<float>(1 + (h % ((1u << 20) - 1)));
}
uint64_t fnv(const void *data, size_t size, uint64_t h = 1469598103934665603ull) {
	const auto *b = static_cast<const unsigned char *>(data);
	for (size_t i = 0; i < size; ++i) {
		h = (h ^ b[i]) * 1099511628211ull;
	}
	return h;
}

// what a body at `kelvin` emits, as linear sRGB against the room's white:
// Planck's law times its emissivity, 1 - reflectance (Kirchhoff), at 1 nm
void glow_of(double kelvin, const Spectrum &reflectance, double rgb[3]) {
	rgb[0] = rgb[1] = rgb[2] = 0.0;
	if (kelvin < 500.0) {
		return; // not a millionth of the room's light
	}
	chem::optics::Xyz xyz;
	for (size_t i = 0; i < reflectance.size(); ++i) {
		const double metres = (chem::optics::FIRST_NM + static_cast<double>(i)) * 1e-9;
		// W / m2 / sr / nm
		const double radiance = 1.191042972e-16 / (metres * metres * metres * metres * metres) / (std::exp(0.014387769 / (metres * kelvin)) - 1.0) * 1e-9;
		const double emitted = radiance * (1.0 - reflectance[i]) * 683.002 / ROOM_WHITE_CD_M2;
		xyz.x += chem::optics::xbar()[i] * emitted;
		xyz.y += chem::optics::ybar()[i] * emitted;
		xyz.z += chem::optics::zbar()[i] * emitted;
	}
	to_linear_srgb(xyz, rgb);
	for (int c = 0; c < 3; ++c) {
		rgb[c] = rgb[c] < 0.0 ? 0.0 : rgb[c];
	}
}

// ---- the long-wave range (GAME_SPEC §12.4, Stage 46)

// Water's and glass's absorption (1/cm) and the index the pass bends and turns by, at each point of a row's long-wave
// segment, from their measured n + ik (data/optics; glass's as fused silica's)
struct LwirMedia {
	std::array<double, LWIR_ROW> water_alpha{}, water_n{}, glass_alpha{}, glass_n{};
};
const LwirMedia &lwir_media() {
	static const LwirMedia m = [] {
		LwirMedia o;
		const auto fill = [](const char *name, std::array<double, LWIR_ROW> &alpha, std::array<double, LWIR_ROW> &n) {
			const chem::SpeciesId id = chem::builtin_species().find(name);
			const chem::optics::LwirSpectrum *nn = id.index >= 0 ? chem::optics::lwir_n(id) : nullptr, *kk = id.index >= 0 ? chem::optics::lwir_k(id) : nullptr;
			for (int i = 0; i < LWIR_ROW; ++i) {
				const size_t j = static_cast<size_t>(i);
				alpha[j] = nn && kk ? chem::optics::absorption_per_cm((*kk)[j], chem::optics::lwir_nm(i)) : 0.0;
				n[j] = nn && kk ? chem::optics::effective_index((*nn)[j], (*kk)[j]) : 1.0;
			}
		};
		fill("water", o.water_alpha, o.water_n);
		fill("glass", o.glass_alpha, o.glass_n);
		return o;
	}();
	return m;
}
// a surface's or a solid's long-wave reflectance: what it does not emit (a non-metal's 0.9 where none is tabled)
double lwir_reflectance(std::string_view name) {
	return 1.0 - chem::optics::lwir_emissivity(name).value_or(0.9);
}

// the depth of liquid of volume `ml` at the bottom of a ball of radius `r` cm
double pool_depth(double ml, double r) {
	double lo = 0.0, hi = 2.0 * r;
	for (int i = 0; i < 40; ++i) {
		const double h = (lo + hi) / 2.0;
		(PI * h * h * (3.0 * r - h) / 3.0 < ml ? lo : hi) = h;
	}
	return (lo + hi) / 2.0;
}

// ---- the spectral viewer's screen (Stage 42)

// the screen's light at full weight, over a whole pixel: each subpixel's share of it
constexpr double SUBPIXEL_SHARE = SCREEN_FILL_X * SCREEN_FILL_Y / 3.0;

// the room's white (D65 at Y = 1): its X, Y and Z, and the sum it is normalised by
struct White {
	double x = 0.0, z = 0.0, norm = 1.0;
};
const White &white_of() {
	static const White w = [] {
		White o;
		double x = 0.0, y = 0.0, z = 0.0;
		for (size_t i = 0; i < chem::optics::d65().size(); ++i) {
			x += chem::optics::d65()[i] * chem::optics::xbar()[i];
			y += chem::optics::d65()[i] * chem::optics::ybar()[i];
			z += chem::optics::d65()[i] * chem::optics::zbar()[i];
		}
		o.x = x / y;
		o.z = z / y;
		o.norm = y;
		return o;
	}();
	return w;
}

// a 3 x 3 matrix's inverse
bool invert3(const double m[3][3], double o[3][3]) {
	const double d = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
	if (!(std::fabs(d) > 1e-300)) {
		return false;
	}
	o[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / d;
	o[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / d;
	o[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / d;
	o[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / d;
	o[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / d;
	o[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / d;
	o[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / d;
	o[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / d;
	o[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / d;
	return true;
}

// The screen's primaries and what a weight of each makes, over a pixel (X, Y, Z against the room's white), and
// the way back
struct Panel {
	std::array<std::array<double, 401>, 3> primaries{};
	double make[3][3] = {}, back[3][3] = {};
};
const Panel &panel() {
	static const Panel p = [] {
		Panel o;
		const char *const names[3] = { "led_red", "led_green", "led_blue" };
		const White &w = white_of();
		double m[3][3] = {};
		for (int c = 0; c < 3; ++c) {
			const Spectrum *e = chem::optics::emitter(names[c]);
			for (size_t i = 0; i < 401; ++i) {
				const double g = e != nullptr ? (*e)[i] : 0.0;
				o.primaries[static_cast<size_t>(c)][i] = g;
				const double light = SUBPIXEL_SHARE * g * chem::optics::d65()[i] / w.norm;
				m[0][c] += light * chem::optics::xbar()[i];
				m[1][c] += light * chem::optics::ybar()[i];
				m[2][c] += light * chem::optics::zbar()[i];
			}
		}
		// each scaled so that all three full are the room's white
		double inv[3][3];
		if (!invert3(m, inv)) {
			return o;
		}
		const double target[3] = { w.x, 1.0, w.z };
		for (int c = 0; c < 3; ++c) {
			const double a = inv[c][0] * target[0] + inv[c][1] * target[1] + inv[c][2] * target[2];
			for (size_t i = 0; i < 401; ++i) {
				o.primaries[static_cast<size_t>(c)][i] *= a;
			}
			for (int r = 0; r < 3; ++r) {
				o.make[r][c] = m[r][c] * a;
			}
		}
		invert3(o.make, o.back);
		return o;
	}();
	return p;
}

double srgb_decoded(uint8_t code) {
	static const std::array<double, 256> table = [] {
		std::array<double, 256> t{};
		for (int i = 0; i < 256; ++i) {
			const double v = i / 255.0;
			t[static_cast<size_t>(i)] = v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
		}
		return t;
	}();
	return table[code];
}

} // namespace

const std::array<std::array<double, 401>, 3> &optics_screen_primaries() {
	return panel().primaries;
}

void optics_screen_drive(const uint8_t code[3], float weights[3]) {
	const Panel &p = panel();
	const double r = srgb_decoded(code[0]), g = srgb_decoded(code[1]), b = srgb_decoded(code[2]);
	// the colour's light (sRGB's primaries, D65 white at Y = 1)
	const double t[3] = { 0.4124564 * r + 0.3575761 * g + 0.1804375 * b, 0.2126729 * r + 0.7151522 * g + 0.0721750 * b, 0.0193339 * r + 0.1191920 * g + 0.9503041 * b };
	const White &w = white_of();
	const double grey[3] = { w.x * t[1], t[1], w.z * t[1] };
	const auto drive = [&](double share, double out[3]) {
		const double m[3] = { grey[0] + (t[0] - grey[0]) * share, t[1], grey[2] + (t[2] - grey[2]) * share };
		bool fits = true;
		for (int c = 0; c < 3; ++c) {
			out[c] = p.back[c][0] * m[0] + p.back[c][1] * m[1] + p.back[c][2] * m[2];
			fits = fits && out[c] >= -1e-9 && out[c] <= 1.0 + 1e-9;
		}
		return fits;
	};
	double out[3];
	if (!drive(1.0, out)) {
		// toward the grey of its luminance, as far as the primaries reach
		double lo = 0.0, hi = 1.0;
		for (int i = 0; i < 30; ++i) {
			const double mid = 0.5 * (lo + hi);
			(drive(mid, out) ? lo : hi) = mid;
		}
		drive(lo, out);
	}
	for (int c = 0; c < 3; ++c) {
		weights[c] = static_cast<float>(std::clamp(out[c], 0.0, 1.0));
	}
}

void optics_compose(OpticsScene &scene, const OpticsRoom &room, const std::vector<OpticsBody> &bodies) {
	std::vector<float> t(static_cast<size_t>(SIZE_VECS) * 4, 0.0f), rows, lines(static_cast<size_t>(MAX_LINES) * 4, 0.0f);
	std::vector<float> lwir; // each set's long-wave rows (Stage 46), appended to `rows` at the end
	const LwirMedia &media = lwir_media();
	const double px_per_cm = room.wall_px / 20.0; // the room's scale at the view: the wall is 20 cm behind the bench
	const auto put = [&](int vec, int part, double v) { t[static_cast<size_t>(vec) * 4 + static_cast<size_t>(part)] = static_cast<float>(v); };
	// how each body lies (Stage 30): the shapes' head carries it to the tracers
	std::vector<double> lies(static_cast<size_t>(MAX_BODIES) * SHAPE_BLOCK * 4, 0.0);
	put(0, 1, room.bench);
	put(0, 2, room.elsewhere);
	put(1, 0, room.x);
	put(1, 1, room.y);
	put(1, 2, room.w);
	put(1, 3, room.h);
	put(2, 0, room.wall_px);
	put(3, 0, room.gap_px);
	put(3, 2, room.cube);
	put(3, 3, room.cube_cell);
	put(4, 0, room.cube_x);
	put(4, 1, room.cube_y);
	put(4, 2, room.kelvin); // Stage 46
	put(5, 0, room.pixel_x0); // the grid, on the room's squares at the view
	put(5, 1, room.pixel_y0);
	put(5, 2, GRID_SQUARE * room.pixel);
	put(4, 3, room.range);
	put(8, 0, room.face_white); // (Stage 78) the faces' ruling
	put(8, 1, room.face_ruled);
	put(8, 2, room.face_line);
	put(8, 3, room.face_pitch);
	for (int k = 0; k < 2; ++k) { // (Stage 61) the screen's panels
		for (int c = 0; c < 4; ++c) {
			put(6 + k, c, room.panels[k][c]);
		}
	}
	put(2, 1, room.front);
	put(2, 2, room.refraction ? 1.0 : 0.0);
	put(2, 3, room.cuts ? 1.0 : 0.0);
	scene.vessels.assign(static_cast<size_t>(MAX_BODIES), nullptr);

	// what the wall's table is made from: the room and everything drawn on it
	uint64_t wall_key = fnv(&room.x, 4 * sizeof(float));
	wall_key = fnv(&room.bench, 2 * sizeof(double), wall_key);
	for (const OpticsShape &sh : room.shapes) {
		wall_key = fnv(&sh.x, 4 * sizeof(float), wall_key);
		wall_key = fnv(&sh.radiance, sizeof(double), wall_key);
	}
	for (const OpticsShape &sh : room.glass_shapes) { // (Stage 61)
		wall_key = fnv(&sh.x, 4 * sizeof(float), wall_key ^ 0x9e3779b97f4a7c15ull);
		wall_key = fnv(&sh.radiance, sizeof(double), wall_key);
	}
	wall_key |= 1u; // never 0: a scene with no table yet
	// a nearer layer's bodies are painted over a further one's: by layer, each layer in the order given
	std::vector<const OpticsBody *> ordered;
	for (const OpticsBody &body : bodies) {
		ordered.push_back(&body);
	}
	const auto layer_of = [](const OpticsBody &b) { return b.layer < 0 ? 0 : b.layer >= OPTICS_LAYERS ? OPTICS_LAYERS - 1 : b.layer; };
	std::stable_sort(ordered.begin(), ordered.end(), [&](const OpticsBody *a, const OpticsBody *b) { return layer_of(*a) < layer_of(*b); });
	int count = 0, line_count = 0, nearest = 0;
	std::map<std::string, int> shared; // the sets of spectra things share, by what they are made of
	// the screens (Stage 42), and where each one's image begins in the images' buffer
	struct ScreenAt {
		const OpticsBody *body;
		int start;
	};
	std::vector<ScreenAt> screens;
	std::vector<ScreenAt> relays; // the sensor displays (Stage 46), and where each one's block begins
	std::vector<int> relay_bodies;
	int image_next = 0;
	for (const OpticsBody *each : ordered) {
		const OpticsBody &body = *each;
		const bool thing = body.shape != OpticsBody::Item;
		if (count == MAX_BODIES || (body.item == nullptr && !thing) || (body.shape != OpticsBody::Bar && (!(body.w > 0.0f) || !(body.h > 0.0f)))) {
			continue;
		}
		nearest = std::max(nearest, layer_of(body));
		if (thing) {
			// a thing of the lab that is no vessel (Stage 22): a shape, of a set of spectra things share
			const int at = HEADER_VECS + count * BODY_VECS;
			++count;
			const bool screen = body.shape == OpticsBody::Screen && body.image != nullptr && body.image_w > 0 && body.image_h > 0;
			const bool sensor = body.shape == OpticsBody::Sensor && body.relay != nullptr;
			const std::string made = screen ? "screen|" + std::to_string(screens.size()) + "|" + (body.surface ? body.surface : "")
											: std::string(body.surface ? body.surface : "") + "|" + (body.light ? body.light : "") + "|" + (body.light_to ? body.light_to : "") + "|" + std::to_string(body.toward) + "|" + std::to_string(body.strength);
			auto found = shared.find(made);
			if (found == shared.end()) {
				const int set = static_cast<int>(rows.size()) / (ROWS_PER_SET * ROW);
				rows.resize(static_cast<size_t>((set + 1) * ROWS_PER_SET * ROW), 0.0f);
				lwir.resize(static_cast<size_t>((set + 1) * ROWS_PER_SET * LWIR_ROW), 0.0f);
				{
					// its long-wave rows (Stage 46): air's indices; what its face does not emit; a flame's absorption per
					// pixel of path (its gas's and its soot's, from the one light toward the other as its own light is)
					float *lw = &lwir[static_cast<size_t>(set * ROWS_PER_SET * LWIR_ROW)];
					const chem::optics::LwirFlame *fa = body.light ? chem::optics::lwir_flame(body.light) : nullptr, *fb = body.light_to ? chem::optics::lwir_flame(body.light_to) : nullptr;
					const double r = body.surface ? lwir_reflectance(body.surface) : 0.0;
					for (int i = 0; i < LWIR_ROW; ++i) {
						const double nm = chem::optics::lwir_nm(i);
						lw[LIQUID_INDEX_ROW * LWIR_ROW + i] = lw[WALL_INDEX_ROW * LWIR_ROW + i] = 1.0f;
						lw[SOLID_ROW * LWIR_ROW + i] = static_cast<float>(r);
						if (body.shape == OpticsBody::Cone && (fa != nullptr || fb != nullptr)) {
							const double from = fa ? fa->per_cm(nm) : 0.0, to = fb ? fb->per_cm(nm) : from;
							lw[LIQUID_ROW * LWIR_ROW + i] = static_cast<float>((from + (to - from) * body.toward) / px_per_cm);
						}
					}
				}
				float *base = &rows[static_cast<size_t>(set * ROWS_PER_SET * ROW)];
				for (int i = 0; i < ROW; ++i) {
					base[LIQUID_INDEX_ROW * ROW + i] = base[WALL_INDEX_ROW * ROW + i] = 1.0f; // air
				}
				if (const Spectrum *r = body.surface ? chem::optics::surface(body.surface) : nullptr) {
					for (int i = 0; i < ROW; ++i) {
						base[SOLID_ROW * ROW + i] = static_cast<float>((*r)[static_cast<size_t>(i)]);
					}
				}
				const Spectrum *a = body.light ? chem::optics::emitter(body.light) : nullptr, *b = body.light_to ? chem::optics::emitter(body.light_to) : nullptr;
				for (int i = 0; i < ROW && screen; ++i) {
					base[GLOW_ROW * ROW + i] = -1.0f; // its light is its image's
				}
				for (int i = 0; i < ROW && (a != nullptr || b != nullptr) && !screen; ++i) {
					const double from = a ? (*a)[static_cast<size_t>(i)] : 0.0, to = b ? (*b)[static_cast<size_t>(i)] : from;
					base[GLOW_ROW * ROW + i] = static_cast<float>(body.strength * (from + (to - from) * body.toward));
				}
				found = shared.emplace(made, set).first;
			}
			float x = body.x, y = body.y, w = body.w, h = body.h;
			if (body.shape == OpticsBody::Bar) {
				x = std::min(body.x0, body.x1) - body.size * 0.5f;
				y = std::min(body.y0, body.y1) - body.size * 0.5f;
				w = std::fabs(body.x1 - body.x0) + body.size;
				h = std::fabs(body.y1 - body.y0) + body.size;
				put(at + MORE, 0, body.x0);
				put(at + MORE, 1, body.y0);
				put(at + MORE, 2, body.x1);
				put(at + MORE, 3, body.y1);
				put(at + FORM, 3, body.dash);
			}
			if (body.shape == OpticsBody::Grating) {
				put(at + MORE, 0, body.size); // its ruling's pitch, nm (Stage 29)
			}
			put(at + RECT, 0, x);
			put(at + RECT, 1, y);
			put(at + RECT, 2, w);
			put(at + RECT, 3, h);
			if (screen) {
				// a slab whose light is its image (Stage 42): where its image will begin in the images' buffer
				put(at + MORE, 0, image_next);
				put(at + MORE, 1, 1.0);
				screens.push_back(ScreenAt{ &body, image_next });
				image_next += IMAGE_HEAD_VECS + (3 * ROW + 3) / 4 + body.image_h * (body.image_w + 1);
			}
			if (sensor) {
				// a sensor's display (Stage 46): where its block begins in the images' buffer
				put(at + MORE, 0, image_next);
				put(at + MORE, 1, 2.0);
				relays.push_back(ScreenAt{ &body, image_next });
				relay_bodies.push_back((at - HEADER_VECS) / BODY_VECS);
				const bool overlaid = body.relay->lcd_w > 0 && body.relay->lcd_h > 0 && body.relay->overlay.size() == static_cast<size_t>(body.relay->lcd_w * body.relay->lcd_h * 3) &&
						body.relay->overlay_mask.size() == static_cast<size_t>(body.relay->lcd_w * body.relay->lcd_h);
				image_next += RELAY_VECS + (overlaid ? body.relay->lcd_w * body.relay->lcd_h : 0);
			}
			put(at + KELVIN_VEC, KELVIN_PART, body.kelvin > 0.0 ? body.kelvin : room.kelvin); // Stage 46
			put(at + HIDDEN_VEC, HIDDEN_PART, body.hidden);
			put(at + FORM, 0, body.shape == OpticsBody::Screen || body.shape == OpticsBody::Sensor ? static_cast<int>(OpticsBody::Slab) : static_cast<int>(body.shape));
			put(at + FORM, 1, 1.0);
			put(at + FORM, 2, body.size);
			put(at + REFRACT, 1, layer_of(body));
			put(at + SET, 0, found->second);
			put(at + IDS, 0, tag(body.id != 0 ? body.id : fnv(made.data(), made.size())));
			uint64_t h_sig = fnv(&rows[static_cast<size_t>(found->second * ROWS_PER_SET * ROW)], ROWS_PER_SET * ROW * sizeof(float));
			h_sig = fnv(&lwir[static_cast<size_t>(found->second * ROWS_PER_SET * LWIR_ROW)], ROWS_PER_SET * LWIR_ROW * sizeof(float), h_sig);
			h_sig = fnv(&t[static_cast<size_t>(at) * 4], 3 * 4 * sizeof(float), h_sig);
			h_sig = fnv(&wall_key, sizeof wall_key, h_sig);
			if (screen) {
				h_sig = fnv(&body.image_hash, sizeof body.image_hash, h_sig); // a new picture is a new colour
			}
			put(at + IDS, 1, tag(h_sig));
			continue;
		}
		const ItemView &v = *body.item;
		const int at = HEADER_VECS + count * BODY_VECS;
		// its own set of spectra
		const int set = static_cast<int>(rows.size()) / (ROWS_PER_SET * ROW);
		rows.resize(static_cast<size_t>((set + 1) * ROWS_PER_SET * ROW), 0.0f);
		lwir.resize(static_cast<size_t>((set + 1) * ROWS_PER_SET * LWIR_ROW), 0.0f);
		float *lw = &lwir[static_cast<size_t>(set * ROWS_PER_SET * LWIR_ROW)];
		for (int i = 0; i < LWIR_ROW; ++i) {
			lw[LIQUID_INDEX_ROW * LWIR_ROW + i] = lw[WALL_INDEX_ROW * LWIR_ROW + i] = 1.0f; // air
		}
		put(at + KELVIN_VEC, KELVIN_PART, body.kelvin > 0.0 ? body.kelvin : v.celsius + 273.15); // Stage 46: its cell's
		put(at + HIDDEN_VEC, HIDDEN_PART, body.hidden);
		put(at + SET, 0, set);
		put(at + SET, 3, body.flipped ? 1.0 : 0.0); // upside down (Stage 27)
		float *liquid = &rows[static_cast<size_t>((set * ROWS_PER_SET + LIQUID_ROW) * ROW)];
		float *solid = &rows[static_cast<size_t>((set * ROWS_PER_SET + SOLID_ROW) * ROW)];
		float *wall = &rows[static_cast<size_t>((set * ROWS_PER_SET + WALL_ROW) * ROW)];
		float *liquid_index = &rows[static_cast<size_t>((set * ROWS_PER_SET + LIQUID_INDEX_ROW) * ROW)];
		float *wall_index = &rows[static_cast<size_t>((set * ROWS_PER_SET + WALL_INDEX_ROW) * ROW)];
		for (int i = 0; i < ROW; ++i) {
			liquid_index[i] = wall_index[i] = 1.0f; // air
		}
		++count;
		const bool ball = v.kind == "balloon";
		const double bottom = static_cast<double>(body.y) + body.h;
		// a vessel made as glassware stands with its axis on a whole pixel (GAME_SPEC §12.3, Stage 24)
		put(at + RECT, 0, v.glassware.empty() ? body.x : room.pixel_x0 + std::round((body.x + body.w / 2.0 - room.pixel_x0) / room.pixel) * room.pixel - body.w / 2.0);
		put(at + RECT, 1, body.y);
		put(at + RECT, 2, body.w);
		put(at + RECT, 3, body.h);
		{
			// Where it stands upright, and how it is turned (Stage 30): upside down is half a turn about its middle.
			// Turned, its rectangle in the scene is the one that holds all of it (the screen's squares and a pixel's
			// path go by it); the tracers go by where it stands upright, turned back.
			double *lie = &lies[static_cast<size_t>((at - HEADER_VECS) / BODY_VECS) * SHAPE_BLOCK * 4];
			const double upright_x = body.angle != 0.0f && !body.flipped ? body.x : t[static_cast<size_t>(at + RECT) * 4], angle = body.flipped ? PI : body.angle;
			const double pivot_x = body.flipped ? upright_x + body.w / 2.0 : body.pivot_x, pivot_y = body.flipped ? body.y + body.h / 2.0 : body.pivot_y;
			lie[3] = angle;
			lie[4] = pivot_x;
			lie[5] = pivot_y;
			lie[6] = std::cos(angle);
			lie[7] = std::sin(angle);
			lie[8] = upright_x;
			lie[9] = body.y;
			lie[10] = body.w;
			lie[11] = body.h;
			lie[12] = -lie[7];
			lie[13] = lie[6];
			if (body.angle != 0.0f && !body.flipped) {
				double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
				for (int corner = 0; corner < 4; ++corner) {
					const double dx = upright_x + (corner % 2 == 1 ? body.w : 0.0f) - pivot_x, dy = body.y + (corner / 2 == 1 ? body.h : 0.0f) - pivot_y;
					const double x = pivot_x + dx * lie[6] - dy * lie[7], y = pivot_y + dx * lie[7] + dy * lie[6];
					x0 = std::min(x0, x);
					y0 = std::min(y0, y);
					x1 = std::max(x1, x);
					y1 = std::max(y1, y);
				}
				put(at + RECT, 0, x0);
				put(at + RECT, 1, y0);
				put(at + RECT, 2, x1 - x0);
				put(at + RECT, 3, y1 - y0);
			}
		}
		put(at + FORM, 0, ball ? 1.0 : 0.0);
		put(at + FORM, 2, bottom + 2.0); // no liquid: its surface is under the floor
		put(at + REFRACT, 1, layer_of(body));
		if (v.kind == "prism") {
			// an equilateral triangle of flint glass, turned; its rectangle is its silhouette
			const double cm_per_px = PRISM_SIDE_CM / body.w, r = PRISM_SIDE_CM / std::sqrt(3.0);
			double cx[3], cz[3], lo = 1e9, hi = -1e9;
			for (int k = 0; k < 3; ++k) {
				const double a = body.turn + PI / 2.0 + 2.0 * PI * k / 3.0;
				cx[k] = r * std::cos(a);
				cz[k] = r * std::sin(a);
				lo = std::min(lo, cx[k]);
				hi = std::max(hi, cx[k]);
			}
			const double centre = body.x + body.w / 2.0, left = centre + lo / cm_per_px, width = (hi - lo) / cm_per_px;
			put(at + RECT, 0, left);
			put(at + RECT, 2, width);
			put(at + FORM, 0, 2.0);
			put(at + FORM, 1, cm_per_px);
			put(at + MORE, 0, cx[0]);
			put(at + MORE, 1, cz[0]);
			put(at + MORE, 2, cx[1]);
			put(at + MORE, 3, cz[1]);
			put(at + GLOW, 0, cx[2]);
			put(at + GLOW, 1, cz[2]);
			put(at + GLOW, 2, centre - (left + width / 2.0));
			if (const Spectrum *n = chem::optics::material_index("n_sf11")) {
				for (size_t i = 0; i < n->size(); ++i) {
					wall_index[i] = static_cast<float>((*n)[i]);
				}
			}
			for (int i = 0; i < LWIR_ROW; ++i) {
				// in the long-wave, its flint glass as fused silica (no long-wave table for it: every oxide glass is opaque there)
				lw[WALL_ROW * LWIR_ROW + i] = static_cast<float>(media.glass_alpha[static_cast<size_t>(i)]);
				lw[WALL_INDEX_ROW * LWIR_ROW + i] = static_cast<float>(media.glass_n[static_cast<size_t>(i)]);
			}
			put(at + REFRACT, 3, room.refraction ? 1.0 : 0.0);
			put(at + IDS, 0, body.named != nullptr ? tag(fnv(body.named, std::strlen(body.named))) : tag(fnv(v.id.data(), v.id.size()))); // (Stage 61: its own name, a supply's box)
			uint64_t h = fnv(&t[static_cast<size_t>(at) * 4], static_cast<size_t>(BODY_VECS) * 4 * sizeof(float));
			h = fnv(&wall_key, sizeof wall_key, h); // what it sees behind it
			put(at + IDS, 1, tag(h));
			continue;
		}

		// ---- what is in it, at 1 nm, and its liquid's narrow lines
		Spectrum reflectance{};
		double solid_g = 0.0, cross_section_cm2 = 0.0;
		bool glass = false;
		const int first_line = line_count;
		for (const SubstanceView &s : v.contents) {
			const chem::SpeciesId id = chem::builtin_species().find(s.species);
			const Kind kind = id.index >= 0 ? chem::optics::kind(id) : Kind::None;
			if (s.wall) {
				if (id.index >= 0 && chem::optics::index_density(id) > 0.0) {
					// a wall with an index: glass (GAME_SPEC §12.3)
					const Spectrum &n = chem::optics::refractive_index(id);
					for (size_t i = 0; i < n.size(); ++i) {
						wall_index[i] = static_cast<float>(n[i]);
					}
					glass = true;
				}
				if (kind == Kind::Transmit) {
					const Spectrum &tr = chem::optics::spectrum(id);
					for (size_t i = 0; i < tr.size(); ++i) {
						wall[i] = static_cast<float>(-std::log(tr[i]));
					}
				}
				// its long-wave rows (Stage 46): glass's absorption and index; a membrane's depth
				for (int i = 0; i < LWIR_ROW; ++i) {
					if (chem::optics::index_density(id) > 0.0) {
						lw[WALL_ROW * LWIR_ROW + i] = static_cast<float>(media.glass_alpha[static_cast<size_t>(i)]);
						lw[WALL_INDEX_ROW * LWIR_ROW + i] = static_cast<float>(media.glass_n[static_cast<size_t>(i)]);
					} else if (kind == Kind::Transmit) {
						lw[WALL_ROW * LWIR_ROW + i] = static_cast<float>(-std::log(std::max(chem::optics::lwir_transmittance(s.species).value_or(1.0), 1e-30)));
					}
				}
				continue;
			}
			if (s.species == "water" && v.liquid_ml > 0.0 && s.phase != 's' && s.phase != 'g') {
				// water in the long-wave (Stage 46): its absorption by its share of the liquid's volume
				for (int i = 0; i < LWIR_ROW; ++i) {
					lw[LIQUID_ROW * LWIR_ROW + i] += static_cast<float>(media.water_alpha[static_cast<size_t>(i)] * (s.ml > 0.0 ? std::min(s.ml / v.liquid_ml, 1.0) : 1.0)); // (no volume given: the liquid is its water)
				}
			}
			if (kind == Kind::Absorb && v.liquid_ml > 0.0 && s.phase != 's' && s.phase != 'g') {
				// (a concentration to 0.2 %, as a layer's: its liquid's volume follows its temperature, Stage 67, and a
				// pixel's history is kept while it drifts by less)
				const double exact = s.mol / (v.liquid_ml / 1000.0);
				const double molar = exact > 0.0 ? std::exp(std::round(std::log(exact) / 0.002) * 0.002) : 0.0;
				const Spectrum &e = chem::optics::spectrum(id);
				for (size_t i = 0; i < e.size(); ++i) {
					liquid[i] += static_cast<float>(e[i] * LN10 * molar);
				}
				for (const chem::optics::Line &l : chem::optics::lines(id)) {
					if (line_count < MAX_LINES) {
						const size_t o = static_cast<size_t>(line_count++) * 4;
						lines[o] = static_cast<float>(l.centre_nm);
						lines[o + 1] = static_cast<float>(l.fwhm_nm);
						lines[o + 2] = static_cast<float>(l.peak * LN10 * molar);
					}
				}
			}
			if (s.phase == 's' && s.g > 0.0) {
				for (size_t i = 0; i < reflectance.size(); ++i) {
					reflectance[i] += s.g * (kind == Kind::Reflect ? chem::optics::spectrum(id)[i] : GREY);
				}
				for (int i = 0; i < LWIR_ROW; ++i) {
					lw[SOLID_ROW * LWIR_ROW + i] += static_cast<float>(s.g * lwir_reflectance(s.species)); // (by mass, below)
				}
				solid_g += s.g;
				const double cm = s.size_um * 1e-4;
				cross_section_cm2 += static_cast<double>(s.particles) * PI * cm * cm / 4.0;
			}
		}
		if (solid_g > 0.0) { // a mixture of powders: by mass
			for (size_t i = 0; i < reflectance.size(); ++i) {
				reflectance[i] /= solid_g;
				solid[i] = static_cast<float>(reflectance[i]);
			}
			for (int i = 0; i < LWIR_ROW; ++i) {
				lw[SOLID_ROW * LWIR_ROW + i] = static_cast<float>(lw[SOLID_ROW * LWIR_ROW + i] / solid_g);
			}
		}
		const double solid_ml = v.solid_ml; // its solids alone: a solute's volume is the solution's (Stage 26)
		if (v.liquid_ml > 0.0 || !v.glassware.empty()) {
			// a liquid's index is its liquids', by their volumes (Stage 78): water's for a solution, mercury's (n + ik)
			// for mercury; the solutes' share is not modelled (GAME_SPEC §20); a glassware vessel's liquid core is
			// water's even while empty (Stage 24): what lies in it says where its liquid is (Stage 30)
			Spectrum n_sum{}, k_sum{};
			double weight = 0.0;
			for (const SubstanceView &s : v.contents) {
				const chem::SpeciesId id = s.wall || s.phase != 'l' || !(s.ml > 0.0) ? chem::SpeciesId{ -1 } : chem::builtin_species().find(s.species);
				if (id.index < 0 || chem::optics::refractive_index(id)[0] == 1.0) {
					continue; // (none known: a liquid of no index is not weighed)
				}
				const Spectrum &n = chem::optics::refractive_index(id), &k = chem::optics::extinction_index(id);
				for (size_t i = 0; i < n.size(); ++i) {
					n_sum[i] += s.ml * n[i];
					k_sum[i] += s.ml * k[i];
				}
				weight += s.ml;
			}
			const Spectrum &water_n = chem::optics::refractive_index(chem::builtin_species().find("water"));
			float *liquid_k = &rows[static_cast<size_t>((set * ROWS_PER_SET + LIQUID_K_ROW) * ROW)];
			for (size_t i = 0; i < water_n.size(); ++i) {
				liquid_index[i] = static_cast<float>(weight > 0.0 ? n_sum[i] / weight : water_n[i]);
				liquid_k[i] = static_cast<float>(weight > 0.0 ? k_sum[i] / weight : 0.0);
			}
			// in the long-wave water's; a metal, known only in the visible, its reddest measured index there (Stage 78,
			// GAME_SPEC §20: liquid mercury turns back 0.78 at 780 nm, more in the long-wave)
			const bool metal = liquid_k[water_n.size() - 1] > 0.0f;
			for (int i = 0; i < LWIR_ROW; ++i) {
				lw[LIQUID_INDEX_ROW * LWIR_ROW + i] = metal ? liquid_index[water_n.size() - 1] : static_cast<float>(media.water_n[static_cast<size_t>(i)]);
				lw[LIQUID_K_ROW * LWIR_ROW + i] = metal ? liquid_k[water_n.size() - 1] : 0.0f;
			}
		}

		// ---- where it is
		if (ball) {
			const double ml = v.gas_ml + v.condensed_ml;
			const double r_cm = std::cbrt(3.0 * (ml > 1.0 ? ml : 1.0) / (4.0 * PI));
			const double cm_per_px = r_cm / (body.w / 2.0);
			put(at + FORM, 1, cm_per_px);
			if (v.condensed_ml > 0.0) {
				put(at + FORM, 2, bottom - pool_depth(v.condensed_ml, r_cm) / cm_per_px);
			}
			// its rubber thins as it stretches: the same rubber over a larger ball
			const double stretch = 30.0 / (body.w / 2.0);
			const double thickness = stretch * stretch;
			put(at + MORE, 1, thickness < 0.125 ? 0.125 : thickness > 2.0 ? 2.0 : thickness);
		} else {
			// glassware in its standard's shape (GAME_SPEC §12.3, Stage 24): its rectangle is its widest by its
			// height; or, made to no standard, a cylinder of its volume and of the proportions it is drawn with
			const glass::Vessel *made = v.glassware.empty() ? nullptr : glass::find(v.glassware, v.nominal_ml);
			scene.vessels[static_cast<size_t>((at - HEADER_VECS) / BODY_VECS)] = made;
			if (made != nullptr) {
				// What lies in it (Stage 30; lab/contents.hpp), in its own frame: its liquid's level in the room and
				// the vortex hanging from it, its sediment's flat section and pile. What is stirred up is in the
				// liquid, not on the floor.
				double *lie = &lies[static_cast<size_t>((at - HEADER_VECS) / BODY_VECS) * SHAPE_BLOCK * 4];
				const double sediment = (1.0 - (v.liquid_ml > 0.0 ? std::min(std::max(v.suspended, 0.0), 1.0) : 0.0)) * v.solid_ml;
				// (Stage 71) what stands in it displaces its liquid: the level the beaker alone would have with that more
				const contents::Lying l = contents::lying(*made, lie[3], std::max(v.condensed_ml - sediment, 0.0) + v.displaced_ml, sediment, body.vortex, body.side);
				// Drawn to a sixty-fourth of a pixel (Stage 33): what lies in a vessel moves by less at every step of
				// the lab (a vortex nearing its size, a liquid losing vapour), and a pixel's rays are followed again
				// whenever what it looks through has moved at all. Nothing a sixty-fourth of a pixel can show is lost.
				const double grain = 2.0 * made->widest() / body.w / 64.0;
				const auto drawn = [&](double cm) { return std::round(cm / grain) * grain; };
				// layered (Stage 47): each layer a band of its own liquid, from the bottom. The layers lie level in the
				// room at any turn, as its surface does: a band's top is the level that holds the liquid of the layers
				// under it, as the surface is the one that holds all of it (in its frame, as lie[15]); tipped to pour,
				// its top layer is the one at the lip, and each under it reaches the lip as the level falls to it.
				std::vector<std::pair<double, double>> held; // (Stage 53) each layer's middle and its temperature, from the bottom
				if (v.layers.size() > 1 && l.liquid) {
					const size_t bands = std::min(v.layers.size(), static_cast<size_t>(MAX_BANDS));
					std::vector<float> table(1 + 3 * bands, 0.0f);
					table[0] = static_cast<float>(bands);
					double layered_ml = 0.0;
					for (const LayerView &layer : v.layers) {
						layered_ml += layer.liquid_ml;
					}
					const double liquid_cm3 = std::max(v.condensed_ml - sediment, 0.0) + v.displaced_ml, scale = layered_ml > 0.0 ? liquid_cm3 / layered_ml : 0.0;
					double below = 0.0, under = -1e9; // the layers' liquid under the band, and its bottom level
					for (size_t k = 0; k < bands; ++k) {
						const LayerView &layer = v.layers[v.layers.size() - 1 - k]; // (the view lists them from the top)
						const int band_set = static_cast<int>(rows.size()) / (ROWS_PER_SET * ROW);
						rows.resize(static_cast<size_t>((band_set + 1) * ROWS_PER_SET * ROW), 0.0f);
						lwir.resize(static_cast<size_t>((band_set + 1) * ROWS_PER_SET * LWIR_ROW), 0.0f);
						lw = &lwir[static_cast<size_t>(set * ROWS_PER_SET * LWIR_ROW)]; // (the resize moves it)
						float *band_lw = &lwir[static_cast<size_t>(band_set * ROWS_PER_SET * LWIR_ROW)];
						for (int i = 0; i < LWIR_ROW; ++i) {
							band_lw[LIQUID_INDEX_ROW * LWIR_ROW + i] = band_lw[WALL_INDEX_ROW * LWIR_ROW + i] = 1.0f; // (only its liquid row is read)
						}
						float *band_liquid = &rows[static_cast<size_t>((band_set * ROWS_PER_SET + LIQUID_ROW) * ROW)];
						for (int i = 0; i < ROW; ++i) {
							band_liquid[LIQUID_INDEX_ROW * ROW + i] = band_liquid[WALL_INDEX_ROW * ROW + i] = 1.0f;
						}
						if (layer.liquid_ml > 0.0) {
							for (const SubstanceView &sv : layer.contents) {
								if (sv.species == "water" && sv.phase != 's' && sv.phase != 'g') {
									// its water in the long-wave (Stage 46), by its share of the layer's volume
									for (int i = 0; i < LWIR_ROW; ++i) {
										band_lw[LIQUID_ROW * LWIR_ROW + i] += static_cast<float>(media.water_alpha[static_cast<size_t>(i)] * (sv.ml > 0.0 ? std::min(sv.ml / layer.liquid_ml, 1.0) : 1.0));
									}
								}
								const chem::SpeciesId id = chem::builtin_species().find(sv.species);
								if (id.index < 0 || sv.phase == 's' || chem::optics::kind(id) != Kind::Absorb) {
									continue;
								}
								// (a concentration to 0.2 %: a gradient spreading slowly keeps its pixels' histories between steps)
								const double molar = sv.mol / (layer.liquid_ml / 1000.0);
								const double kept = molar > 0.0 ? std::exp(std::round(std::log(molar) / 0.002) * 0.002) : 0.0;
								const Spectrum &e = chem::optics::spectrum(id);
								for (size_t i = 0; i < e.size(); ++i) {
									band_liquid[i] += static_cast<float>(e[i] * LN10 * kept);
								}
							}
						}
						below += layer.liquid_ml;
						const double over = k + 1 == bands ? 1e9 : drawn(contents::lying(*made, lie[3], below * scale, sediment, 0.0, body.side).liquid_level);
						table[1 + 3 * k] = static_cast<float>(band_set);
						table[2 + 3 * k] = static_cast<float>(under);
						table[3 + 3 * k] = static_cast<float>(std::max(over, under));
						held.emplace_back(0.5 * ((k == 0 ? made->floor : under) + (k + 1 == bands ? drawn(l.liquid_level) : std::max(over, under))), layer.celsius + 273.15);
						under = std::max(over, under);
					}
					float *band_table = &rows[static_cast<size_t>((set * ROWS_PER_SET + BANDS_ROW) * ROW)];
					std::copy(table.begin(), table.end(), band_table);
					// (the rows grew: the body's own rows are where they are now, not where they were)
					liquid = &rows[static_cast<size_t>((set * ROWS_PER_SET + LIQUID_ROW) * ROW)];
					solid = &rows[static_cast<size_t>((set * ROWS_PER_SET + SOLID_ROW) * ROW)];
					wall = &rows[static_cast<size_t>((set * ROWS_PER_SET + WALL_ROW) * ROW)];
					liquid_index = &rows[static_cast<size_t>((set * ROWS_PER_SET + LIQUID_INDEX_ROW) * ROW)];
					wall_index = &rows[static_cast<size_t>((set * ROWS_PER_SET + WALL_INDEX_ROW) * ROW)];
				}
				lie[2] = 1.0;
				lie[12] = l.up_x;
				lie[13] = l.up_y;
				lie[14] = l.liquid ? 1.0 : 0.0;
				lie[15] = drawn(l.liquid_level);
				lie[16] = drawn(l.vortex.height);
				lie[17] = drawn(l.vortex.across);
				lie[18] = drawn(l.vortex.deep);
				lie[19] = l.solid ? 1.0 : 0.0;
				lie[20] = drawn(l.vortex.x);
				lie[21] = drawn(l.vortex.y);
				lie[22] = drawn(l.solid_level);
				lie[23] = drawn(l.pile.height);
				lie[24] = drawn(l.pile.across);
				lie[25] = drawn(l.pile.deep);
				lie[26] = drawn(l.pile.x);
				lie[27] = drawn(l.pile.y);
				// (Stage 53) its parts' temperatures by height: its wall's rings, each at its own; its layers, each at its
				// own (a vessel of one layer: its one temperature, the body's)
				if (!(body.kelvin > 0.0)) {
					const size_t temps = static_cast<size_t>((set * ROWS_PER_SET + TEMPS_ROW) * ROW);
					// (the signature a weighted mean, on the scale of one temperature: a pixel's key is a float's sum, and a
					// larger term would drown its others)
					double hottest = v.celsius + 273.15, signature = hottest, weights = 1.0;
					int n = 0;
					const std::vector<walls::Ring> &rings = rings_cut(made);
					if (!rings.empty() && v.wall_c.size() == rings.size() && rings.size() <= static_cast<size_t>(MAX_RINGS) + 1) {
						rows[temps] = static_cast<float>(rings.size());
						for (size_t k = 0; k < rings.size(); ++k) {
							const double kelvin = v.wall_c[k] + 273.15;
							rows[temps + 1 + 2 * k] = static_cast<float>(0.5 * (rings[k].from + rings[k].to));
							rows[temps + 2 + 2 * k] = static_cast<float>(kelvin);
							hottest = std::max(hottest, kelvin);
							signature += kelvin * (1.0 + 0.0137 * ++n);
							weights += 1.0 + 0.0137 * n;
						}
					}
					if (held.size() > 1) {
						rows[temps + TEMPS_CONTENTS] = static_cast<float>(held.size());
						for (size_t k = 0; k < held.size(); ++k) {
							rows[temps + TEMPS_CONTENTS + 1 + 2 * k] = static_cast<float>(held[k].first);
							rows[temps + TEMPS_CONTENTS + 2 + 2 * k] = static_cast<float>(held[k].second);
							hottest = std::max(hottest, held[k].second);
							signature += held[k].second * (1.0 + 0.0137 * ++n);
							weights += 1.0 + 0.0137 * n;
						}
					}
					rows[temps + TEMPS_HOTTEST] = static_cast<float>(hottest);
					rows[temps + TEMPS_HOTTEST + 1] = static_cast<float>(signature / weights);
				}
			}
			const double aspect = static_cast<double>(body.h) / body.w;
			const double d_cm = made ? 2.0 * made->widest() : std::cbrt(4.0 * (v.volume_ml > 0.0 ? v.volume_ml : 1.0) / (PI * aspect));
			const double cm_per_px = d_cm / body.w;
			const double floor_cm2 = PI * d_cm * d_cm / 4.0;
			put(at + FORM, 1, cm_per_px);
			if (room.refraction && glass) {
				// its glass: as thick as its vessel is made (ISO 3819's walls, docs/DATA_SOURCES.md), not its
				// heat's mass spread thin; a wall as thick as the vessel is wide is glass all through
				put(at + REFRACT, 0, v.wall_mm / 10.0);
			}
			// its floor: glass all through, as high as it is thick (at most the vessel's height); what it holds
			// rests on it
			const double floor_cm = glass ? std::min(v.floor_mm / 10.0, d_cm * aspect) : 0.0;
			put(at + REFRACT, 2, floor_cm);
			put(at + REFRACT, 3, room.refraction ? 1.0 : 0.0);
			if (v.liquid_ml > 0.0) {
				// its level: where the inside below holds what is condensed (a cone fills faster as it narrows)
				// upside down (Stage 27), it lies from its mouth, now at the bottom
				put(at + FORM, 2, bottom - (made && body.flipped ? made->head_inverted(v.condensed_ml) : made ? made->level(v.condensed_ml) : floor_cm + v.condensed_ml / floor_cm2) / cm_per_px);
			}
			// what the liquid holds up of its solids (Stage 32), and what lies at rest
			const double held_up = v.liquid_ml > 0.0 ? std::min(std::max(v.suspended, 0.0), 1.0) : 0.0, at_rest = (1.0 - held_up) * solid_ml;
			if (solid_ml > 0.0 && held_up > 0.0) {
				// stirred up: every particle shades its own cross-section, twice over
				put(at + MORE, 0, EXTINCTION_EFFICIENCY * held_up * cross_section_cm2 / v.liquid_ml);
			}
			// boiling's bubbles (Stage 45): steam's from the floor up to where they end, a reaction's gas's all the way
			// up; each a haze, white, of their extinction times 1 - g (they scatter mostly forward). Quantised (steps of 3 % in extinction, a twentieth in the exponent, a hundredth
			// of a millimetre in height), so a pixel keeps its history while a boil stands still (GAME_SPEC §12.2)
			if (!body.flipped && (v.bubble_extinction > 0.0 || v.gas_extinction > 0.0)) {
				// what they turn back at each wavelength: their albedo, as the optics has them
				const chem::optics::Spectrum &albedo = *chem::optics::scatterer("bubbles")->albedo;
				float *bubble_row = &rows[static_cast<size_t>((set * ROWS_PER_SET + BUBBLE_ROW) * ROW)];
				for (int i = 0; i < ROW; ++i) {
					bubble_row[i] = static_cast<float>(albedo[static_cast<size_t>(i)]);
				}
				const auto steps = [](double x) { return x > 0.0 ? std::exp(std::round(std::log(x) / 0.03) * 0.03) : 0.0; };
				const double back = 1.0 - bubbles::asymmetry();
				put(at + 6, 0, steps(v.bubble_extinction * back));
				put(at + 6, 1, std::round(v.bubbles_end_cm * 1000.0) / 1000.0);
				put(at + 6, 2, std::round(v.bubble_exponent * 20.0) / 20.0);
				put(at + 6, 3, steps(v.gas_extinction * back));
			}
			if (at_rest > 0.0) {
				// at rest: a heap at its angle of repose, a cone until it reaches the wall
				const double heap_ml = at_rest / PACKING, r_wall = made ? made->radius_inside(made->floor + 0.25 * made->height) : d_cm / 2.0;
				double r_base = std::cbrt(3.0 * heap_ml / (PI * REPOSE));
				if (r_base > r_wall) {
					r_base = (heap_ml / (2.0 * PI * REPOSE) + r_wall * r_wall * r_wall / 3.0) * 2.0 / (r_wall * r_wall);
				}
				put(at + FORM, 3, r_base * REPOSE / cm_per_px);
				put(at + MORE, 2, r_base / cm_per_px);
			}
		}
		if (solid_g > 0.0) {
			double rgb[3];
			glow_of((v.layers.empty() ? v.celsius : v.layers.back().celsius) + 273.15, reflectance, rgb); // (its solids lie in its lowest layer)
			for (int c = 0; c < 3; ++c) {
				put(at + GLOW, c, rgb[c]); // its colour: a pixel's history follows it
			}
		}
		// which body this is, and a signature of what it holds and sees: a pixel's history follows them (GAME_SPEC §12.2)
		put(at + IDS, 0, body.named != nullptr ? tag(fnv(body.named, std::strlen(body.named))) : tag(fnv(v.id.data(), v.id.size()))); // (Stage 61: its own name, a supply's box)
		uint64_t h = fnv(liquid, (ROWS_PER_SET - 1) * ROW * sizeof(float)); // (its temperatures not: its key follows them where they show, Stage 53)
		h = fnv(lw, ROWS_PER_SET * LWIR_ROW * sizeof(float), h);
		h = fnv(&t[static_cast<size_t>(at + REFRACT) * 4], 4 * sizeof(float), h);
		h = fnv(&wall_key, sizeof wall_key, h); // what it sees behind it
		h = fnv(&lines[static_cast<size_t>(first_line) * 4], static_cast<size_t>(line_count - first_line) * 4 * sizeof(float), h);
		{
			// how it lies (Stage 30): its turn, and what lies in it (the liquid's level, the vortex, the sediment)
			const double *lie = &lies[static_cast<size_t>((at - HEADER_VECS) / BODY_VECS) * SHAPE_BLOCK * 4];
			h = fnv(&lie[2], 2 * sizeof(double), h);
			h = fnv(&lie[12], 16 * sizeof(double), h);
		}
		put(at + IDS, 1, tag(h));
		put(at + IDS, 2, first_line);
		put(at + IDS, 3, line_count - first_line);
	}
	put(0, 0, count);
	// The images (Stage 42): made again only when one has changed, or where they lie
	{
		uint64_t key = 1469598103934665603ull;
		for (const ScreenAt &sc : screens) {
			key = fnv(&sc.body->image_hash, sizeof sc.body->image_hash, key);
			key = fnv(&sc.body->image_w, sizeof sc.body->image_w, key);
			key = fnv(&sc.body->image_h, sizeof sc.body->image_h, key);
			key = fnv(&sc.start, sizeof sc.start, key);
		}
		for (const ScreenAt &sc : relays) {
			key = fnv(&sc.start, sizeof sc.start, key); // (a sensor display's block is written below, every time)
			key = fnv(&sc.body->relay->lcd_w, 2 * sizeof(int), key);
		}
		key = fnv(&image_next, sizeof image_next, key);
		if (key != scene.images_key || ((screens.empty() && relays.empty()) != scene.images.empty())) {
			scene.images.assign(static_cast<size_t>(std::max(image_next, 1)) * 4, 0.0f);
			scene.image_weights.clear();
			const std::array<std::array<double, 401>, 3> &light = panel().primaries;
			for (const ScreenAt &sc : screens) {
				const OpticsBody &b = *sc.body;
				const int w = b.image_w, h = b.image_h;
				float *head = &scene.images[static_cast<size_t>(sc.start) * 4];
				const int channels = sc.start + IMAGE_HEAD_VECS, sums = channels + (3 * ROW + 3) / 4;
				head[0] = static_cast<float>(w);
				head[1] = static_cast<float>(h);
				head[2] = static_cast<float>(SCREEN_FILL_X);
				head[3] = static_cast<float>(SCREEN_FILL_Y);
				head[4] = static_cast<float>(channels);
				head[5] = static_cast<float>(sums);
				head[6] = static_cast<float>(scene.image_weights.size());
				for (int c = 0; c < 3; ++c) {
					for (int i = 0; i < ROW; ++i) {
						scene.images[static_cast<size_t>(channels) * 4 + static_cast<size_t>(c * ROW + i)] = static_cast<float>(light[static_cast<size_t>(c)][static_cast<size_t>(i)]);
					}
				}
				// (what the panel drives its primaries to for each colour, worked out once a colour: a picture has few)
				thread_local std::unordered_map<uint32_t, std::array<float, 3>> drives;
				if (drives.size() > 65536) {
					drives.clear();
				}
				for (int y = 0; y < h; ++y) {
					float *row = &scene.images[static_cast<size_t>(sums + y * (w + 1)) * 4];
					double run[3] = { 0.0, 0.0, 0.0 };
					for (int x = 0; x < w; ++x) {
						const uint8_t *code = &b.image[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 3];
						const uint32_t colour = static_cast<uint32_t>(code[0]) << 16 | static_cast<uint32_t>(code[1]) << 8 | code[2];
						auto found = drives.find(colour);
						if (found == drives.end()) {
							std::array<float, 3> d{};
							optics_screen_drive(code, d.data());
							found = drives.emplace(colour, d).first;
						}
						const float *drive = found->second.data();
						for (int c = 0; c < 3; ++c) {
							scene.image_weights.push_back(drive[c]);
							run[c] += drive[c];
							row[static_cast<size_t>(x + 1) * 4 + static_cast<size_t>(c)] = static_cast<float>(run[c]);
						}
					}
				}
			}
			scene.images_key = key;
			scene.images_revision = next_revision();
		}
		// The sensor displays' blocks (Stage 46), written whenever anything in them changes: where their frames are,
		// their settings, and a tag of what lies under each frame (a pixel of the display follows it)
		bool changed = false;
		int pool = 0;
		for (size_t r = 0; r < relays.size(); ++r) {
			const OpticsBody &b = *relays[r].body;
			const OpticsRelay &relay = *b.relay;
			const int body = relay_bodies[r];
			const float *rect = &t[static_cast<size_t>(HEADER_VECS + body * BODY_VECS + RECT) * 4];
			const bool overlaid = relay.lcd_w > 0 && relay.lcd_h > 0 && relay.overlay.size() == static_cast<size_t>(relay.lcd_w * relay.lcd_h * 3) &&
					relay.overlay_mask.size() == static_cast<size_t>(relay.lcd_w * relay.lcd_h);
			const int lcd = overlaid ? relay.lcd_w * relay.lcd_h : 0;
			std::vector<float> block(static_cast<size_t>(RELAY_VECS + lcd) * 4, 0.0f);
			const double room_signal = chem::optics::band_signal(room.kelvin);
			const double reflected = relay.reflected_kelvin > 0.0 ? relay.reflected_kelvin : room.kelvin;
			block[0] = relay.frame_x - rect[0];
			block[1] = relay.frame_y - rect[1];
			block[2] = static_cast<float>(relay.emissivity);
			block[3] = static_cast<float>(chem::optics::band_signal(reflected) / room_signal);
			block[4] = static_cast<float>(relay.low_kelvin);
			block[5] = static_cast<float>(relay.high_kelvin);
			block[6] = static_cast<float>(relay.bit);
			block[7] = static_cast<float>(room.pixel);
			// its pixels' records: its rectangle as far as it is on the screen
			const int left = std::clamp(static_cast<int>(std::floor(rect[0])), 0, WALL_COLUMNS), top = std::clamp(static_cast<int>(std::floor(rect[1])), 0, WALL_ROWS);
			const int right = std::clamp(static_cast<int>(std::ceil(rect[0] + rect[2])), 0, WALL_COLUMNS), bottom = std::clamp(static_cast<int>(std::ceil(rect[1] + rect[3])), 0, WALL_ROWS);
			block[8] = static_cast<float>(pool);
			block[9] = static_cast<float>(left);
			block[10] = static_cast<float>(top);
			block[11] = static_cast<float>(right - left);
			block[13] = static_cast<float>(bottom - top);
			block[14] = static_cast<float>(SCREEN_FILL_X); // its subpixels, as the viewer's
			block[15] = static_cast<float>(SCREEN_FILL_Y);
			if (overlaid) {
				// what it shows over its picture (Stage 48): each LCD pixel's drive, and how much of it the overlay is
				block[16] = static_cast<float>(relay.lcd_w);
				block[17] = static_cast<float>(relay.lcd_h);
				block[18] = static_cast<float>(relays[r].start + RELAY_VECS);
				thread_local std::unordered_map<uint32_t, std::array<float, 3>> drives;
				for (int k = 0; k < lcd; ++k) {
					float *to = &block[static_cast<size_t>(RELAY_VECS + k) * 4];
					to[3] = relay.overlay_mask[static_cast<size_t>(k)];
					if (!(to[3] > 0.0f)) {
						continue;
					}
					const uint8_t *code = &relay.overlay[static_cast<size_t>(k) * 3];
					const uint32_t colour = static_cast<uint32_t>(code[0]) << 16 | static_cast<uint32_t>(code[1]) << 8 | code[2];
					auto found = drives.find(colour);
					if (found == drives.end()) {
						std::array<float, 3> d{};
						optics_screen_drive(code, d.data());
						found = drives.emplace(colour, d).first;
					}
					to[0] = found->second[0];
					to[1] = found->second[1];
					to[2] = found->second[2];
				}
			}
			pool += (right - left) * (bottom - top);
			// what lies under its frame: each body there, where it is, what it is and holds, how hot (none hidden from it)
			// (what its sensor reads: what lies under its frame, where its frame is, the room; not how it shows it, which
			// changes what it shows and reads nothing again: Stage 48)
			uint64_t under = fnv(&relay.frame_x, 2 * sizeof(float));
			under = fnv(&room.kelvin, sizeof room.kelvin, under);
			under = fnv(&relay.bit, sizeof relay.bit, under);
			for (int o = 0; o < count; ++o) {
				const size_t at = static_cast<size_t>(HEADER_VECS + o * BODY_VECS) * 4;
				const float *other = &t[at + RECT * 4];
				const bool hidden = (static_cast<uint32_t>(t[at + HIDDEN_VEC * 4 + HIDDEN_PART]) & relay.bit) != 0;
				if (!hidden && other[0] < relay.frame_x + rect[2] + 2.0f && other[0] + other[2] + 2.0f > relay.frame_x && other[1] < relay.frame_y + rect[3] + 2.0f && other[1] + other[3] + 2.0f > relay.frame_y) {
					under = fnv(&t[at], 4 * 4 * sizeof(float), under); // its rectangle, form, more and glow
					under = fnv(&t[at + IDS * 4], 2 * sizeof(float), under);
					under = fnv(&t[at + KELVIN_VEC * 4 + KELVIN_PART], sizeof(float), under);
				}
			}
			block[12] = tag(under);
			// what its primaries are driven to for each colour of its palette
			for (int k = 0; k < RELAY_PALETTE; ++k) {
				uint8_t code[3];
				for (int c = 0; c < 3; ++c) {
					const double v = std::clamp(static_cast<double>(relay.palette[static_cast<size_t>(k)][static_cast<size_t>(c)]), 0.0, 1.0);
					code[c] = static_cast<uint8_t>(std::lround(255.0 * (v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055)));
				}
				optics_screen_drive(code, &block[static_cast<size_t>(RELAY_HEAD_VECS + k) * 4]);
			}
			const std::array<std::array<double, 401>, 3> &light = panel().primaries;
			for (int c = 0; c < 3; ++c) {
				for (int i = 0; i < ROW; ++i) {
					block[static_cast<size_t>(RELAY_HEAD_VECS + RELAY_PALETTE) * 4 + static_cast<size_t>(c * ROW + i)] = static_cast<float>(light[static_cast<size_t>(c)][static_cast<size_t>(i)]);
				}
			}
			float *to = &scene.images[static_cast<size_t>(relays[r].start) * 4];
			if (!std::equal(block.begin(), block.end(), to)) {
				std::copy(block.begin(), block.end(), to);
				changed = true;
			}
			// its pixels follow what lies under its frame, and how it shows it (its settings, its span, its overlay)
			const size_t ids = static_cast<size_t>(HEADER_VECS + body * BODY_VECS + IDS) * 4 + 1;
			uint64_t shows = fnv(&block[12], sizeof(float), static_cast<uint64_t>(t[ids]));
			shows = fnv(&relay.hash, sizeof relay.hash, shows);
			shows = fnv(&relay.overlay_hash, sizeof relay.overlay_hash, shows);
			t[ids] = tag(shows);
		}
		scene.relay_pool = pool;
		if (changed) {
			scene.images_revision = next_revision();
		}
	}

	if (scene.lights.empty() || scene.lights_kelvin != room.kelvin) {
		scene.lights.assign(static_cast<size_t>(LIGHTS * LIGHT_SIZE) * 4, 0.0f);
		// the visible room's light; and (Stage 46) the long-wave room's, at the room's temperature
		const chem::optics::EnergyTable lwir_light = chem::optics::lwir_energy_table(room.kelvin);
		for (int light = 0; light < LIGHTS; ++light) {
			const chem::optics::EnergyTable &table = light == 0 ? chem::optics::room_light() : lwir_light;
			float *at = &scene.lights[static_cast<size_t>(light * LIGHT_SIZE) * 4];
			// (the long-wave light's colour is its signal as the room's white: a grey, so a pixel's sum in linear sRGB is
			// its signal in each channel)
			const double wx = light == 0 ? 1.0 : white_of().x, wz = light == 0 ? 1.0 : white_of().z;
			for (size_t j = 0; j < static_cast<size_t>(LIGHT_ENTRIES); ++j) {
				const chem::optics::EnergyEntry &e = table.entry[j];
				at[4 * j] = static_cast<float>(e.nm - chem::optics::FIRST_NM);
				at[4 * j + 1] = static_cast<float>(e.up_to.x * wx);
				at[4 * j + 2] = static_cast<float>(e.up_to.y);
				at[4 * j + 3] = static_cast<float>(e.up_to.z * wz);
			}
			// the table the other way: the share of the energy up to each whole nm (each 20 nm from 8000 nm in the
			// long-wave; the entries ascend in both), and the light's radiance there, W / m2 / sr / nm
			const int points = light == 0 ? ROW : LWIR_ROW;
			size_t j = 0;
			for (int i = 0; i < points; ++i) {
				const double nm = light == 0 ? FIRST_NM_D + i : chem::optics::lwir_nm(i), x = nm - chem::optics::FIRST_NM;
				while (j + 2 < static_cast<size_t>(LIGHT_ENTRIES) && at[4 * (j + 1)] <= x) {
					++j;
				}
				const double lo = at[4 * j], hi = at[4 * (j + 1)];
				const double part = hi > lo ? std::min(std::max((x - lo) / (hi - lo), 0.0), 1.0) : 0.0;
				float *back = &at[4 * (static_cast<size_t>(LIGHT_ENTRIES) + static_cast<size_t>(i))];
				back[0] = static_cast<float>((static_cast<double>(j) + part) / (LIGHT_ENTRIES - 1));
				back[1] = static_cast<float>(light == 0 ? chem::optics::d65()[static_cast<size_t>(i)] / white_of().norm * ROOM_WHITE_CD_M2 / 683.002 : chem::optics::planck(nm, room.kelvin));
			}
		}
		{
			// the temperature of a signal (on the room's scale), at 400 places evenly in its logarithm from 0.01 to 1000:
			// the debug infrared view's (after the long-wave light's table the other way)
			float *t_of = &scene.lights[static_cast<size_t>(LIGHT_SIZE + LIGHT_ENTRIES + LWIR_ROW) * 4];
			const double room_signal = chem::optics::band_signal(room.kelvin);
			for (int k = 0; k < 400; ++k) {
				const double signal = 0.01 * std::exp(std::log(1.0e5) * k / 399.0);
				t_of[k] = static_cast<float>(chem::optics::brightness_kelvin(signal * room_signal, 1.0, room.kelvin));
			}
		}
		scene.lights_kelvin = room.kelvin;
		scene.revision = next_revision();
	}
	// the wall's table (GAME_SPEC §12.3): made again only when what is drawn on it changes
	if (scene.wall_key != wall_key || scene.wall.empty()) {
		// (Stage 61) the wall as drawn, then (with anything drawn behind glass only) the wall behind glass after it
		scene.drawn_glass.clear();
		if (!room.glass_shapes.empty()) {
			OpticsRoom behind = room;
			behind.shapes.insert(behind.shapes.end(), room.glass_shapes.begin(), room.glass_shapes.end());
			optics_wall(scene, behind);
			const std::vector<float> glass = scene.wall;
			for (const OpticsShape &sh : room.glass_shapes) {
				scene.drawn_glass.insert(scene.drawn_glass.end(), { sh.x, sh.y, sh.w, sh.h, static_cast<float>(sh.radiance) });
			}
			optics_wall(scene, room);
			scene.wall_glass_base = static_cast<int>(scene.wall.size() / 4);
			scene.wall.insert(scene.wall.end(), glass.begin(), glass.end());
		} else {
			optics_wall(scene, room);
			scene.wall_glass_base = 0;
		}
		scene.wall_key = wall_key;
		scene.wall[2] = tag(wall_key); // the pixels that show the wall alone follow it
	}
	put(5, 3, scene.wall_glass_base); // (Stage 61)
	put(3, 1, nearest);
	// The rectangles the passes over the pixels read by their own rays run over (Stage 29): one for every body that
	// bends, and every grating; with the table, so the scene's revision covers them
	scene.rectangles.clear();
	scene.rectangle_bodies.clear();
	for (int b = 0; b < count; ++b) {
		const float *rect = &t[static_cast<size_t>(HEADER_VECS + b * BODY_VECS + RECT) * 4], *refract = &t[static_cast<size_t>(HEADER_VECS + b * BODY_VECS + REFRACT) * 4];
		const float *form = &t[static_cast<size_t>(HEADER_VECS + b * BODY_VECS + FORM) * 4];
		if (form[0] > 5.5f || (refract[3] > 0.5f && (form[0] < 0.5f || form[0] > 1.5f))) {
			scene.rectangles.insert(scene.rectangles.end(), { std::floor(rect[0] - 2.0f), std::floor(rect[1] - 2.0f), std::ceil(rect[0] + rect[2] + 2.0f), std::ceil(rect[1] + rect[3] + 2.0f) });
			scene.rectangle_bodies.push_back(b);
		}
	}
	// (Stage 78) a body whose liquid is a metal: a mirror, whose rays may come to any other body in the room
	const auto mirror = [&](int b) {
		const size_t set = static_cast<size_t>(t[static_cast<size_t>(HEADER_VECS + b * BODY_VECS + SET) * 4]);
		if ((set + 1) * static_cast<size_t>(ROWS_PER_SET * ROW) > rows.size()) {
			return false;
		}
		const float *k = &rows[(set * ROWS_PER_SET + LIQUID_K_ROW) * ROW];
		return k[0] > 0.0f || k[ROW / 2] > 0.0f || k[ROW - 1] > 0.0f;
	};
	// What stands behind each body, in its rows, on a layer further from the eye: a signature of it (where it
	// stands, what it is and what it holds), which a pixel's history follows (GAME_SPEC §12.3): a pixel of a held
	// vessel whose rays reach a body that has changed colour is read again (Stage 78: a mirror's, every other body)
	for (int b = 0; b < count; ++b) {
		const bool all = mirror(b);
		const float *rect = &t[static_cast<size_t>(HEADER_VECS + b * BODY_VECS + RECT) * 4];
		const float layer = t[static_cast<size_t>(HEADER_VECS + b * BODY_VECS + REFRACT) * 4 + 1];
		bool lower = false;
		uint64_t h = 1469598103934665603ull;
		for (int o = 0; o < count; ++o) {
			const float *other = &t[static_cast<size_t>(HEADER_VECS + o * BODY_VECS + RECT) * 4];
			if (all ? o != b : t[static_cast<size_t>(HEADER_VECS + o * BODY_VECS + REFRACT) * 4 + 1] < layer && other[1] < rect[1] + rect[3] + 1.0f && other[1] + other[3] + 1.0f > rect[1]) {
				lower = true;
				h = fnv(other, 4 * sizeof(float), h);
				h = fnv(&t[static_cast<size_t>(HEADER_VECS + o * BODY_VECS + FORM) * 4], 4 * sizeof(float), h);
				h = fnv(&t[static_cast<size_t>(HEADER_VECS + o * BODY_VECS + IDS) * 4], 2 * sizeof(float), h);
			}
		}
		put(HEADER_VECS + b * BODY_VECS + SET, 2, lower ? tag(h) : 0.0);
	}
	// Each body's tags (Stage 33; the shapes' head, its vec4s 7 to 9). Its shape tag: everything the way of a
	// ray through its glass depends on, and nothing of its colour: where it stands and how (its rectangle, form,
	// glass, layer), how it lies, its glass's and its liquid's indices, the vessel it is made as. Its content
	// tag, where what lies in it is given: the liquid's level and its vortex, the sediment and its pile; now,
	// and as the scene before this had it, with the rows of the screen between which the two differ (a level
	// that rises changes nothing under where it was or over where it is). And for what stands behind it, in its
	// rows, on a layer further from the eye, which its rays can reach: a signature of the shape tags, one of the
	// content tags now and before, and all the rows that changed. A pixel's beams are kept while the shape tags
	// of what it looks through are the same and what lies there has changed only on rows its rays do not pass.
	{
		const auto lie_of = [&](const std::vector<double> &from, int b) { return &from[static_cast<size_t>(b) * SHAPE_BLOCK * 4]; };
		std::vector<float> shape_tag(static_cast<size_t>(count), 0.0f), content_tag(static_cast<size_t>(count), 0.0f), content_was(static_cast<size_t>(count), 0.0f);
		std::vector<double> zone_lo(static_cast<size_t>(count), 1.0e30), zone_hi(static_cast<size_t>(count), -1.0e30);
		// the scene before: each body by its id
		const bool had = scene.table.size() == t.size() && scene.lies.size() == lies.size();
		const int count_was = had ? std::min(static_cast<int>(scene.table[0]), MAX_BODIES) : 0;
		std::vector<int> before(static_cast<size_t>(count), -1);
		bool as_before = had && count_was == count;
		for (int b = 0; b < count; ++b) {
			const size_t at = static_cast<size_t>(HEADER_VECS + b * BODY_VECS) * 4;
			const double *lie = lie_of(lies, b);
			const bool given = lie[2] > 0.5; // what lies in it is traced: its liquid's surface and its heap are not the glass's
			uint64_t h = fnv(&t[at + RECT * 4], 4 * sizeof(float));
			h = fnv(&t[at + FORM * 4], (given ? 2 : 4) * sizeof(float), h);
			if (!given) {
				h = fnv(&t[at + MORE * 4], 4 * sizeof(float), h);
			}
			h = fnv(&t[at + REFRACT * 4], 4 * sizeof(float), h);
			if (t[at + FORM * 4] > 1.5f) {
				h = fnv(&t[at + GLOW * 4], 4 * sizeof(float), h); // (a prism's third corner)
			}
			const size_t set = static_cast<size_t>(t[at + SET * 4]);
			if ((set + 1) * static_cast<size_t>(ROWS_PER_SET * ROW) <= rows.size()) {
				h = fnv(&rows[(set * ROWS_PER_SET + 3) * ROW], 2 * ROW * sizeof(float), h); // its liquid's index and its glass's
				h = fnv(&rows[(set * ROWS_PER_SET + LIQUID_K_ROW) * ROW], ROW * sizeof(float), h); // and its liquid's k (Stage 78)
			}
			h = fnv(lie + 2, (given ? 12 : 26) * sizeof(double), h);
			const glass::Vessel *made = scene.vessels[static_cast<size_t>(b)];
			h = fnv(&made, sizeof made, h);
			shape_tag[static_cast<size_t>(b)] = tag(h);
			// (Stage 65: and its liquid's surface row, FORM z, which says where a ray is in its liquid's core; it
			// follows the vessel's condensed volume, not what lies in it, so a level that stands while the volume
			// moves left a kept ray's route as it was)
			content_tag[static_cast<size_t>(b)] = given ? tag(fnv(&t[at + FORM * 4 + 2], sizeof(float), fnv(lie + 14, 14 * sizeof(double)))) : 0.0f;
			for (int o = 0; o < count_was; ++o) {
				if (scene.table[static_cast<size_t>(HEADER_VECS + o * BODY_VECS + IDS) * 4] == t[at + IDS * 4]) {
					before[static_cast<size_t>(b)] = o;
					break;
				}
			}
			const int o = before[static_cast<size_t>(b)];
			as_before = as_before && o == b && static_cast<float>(lie_of(scene.lies, o)[30]) == content_tag[static_cast<size_t>(b)];
		}
		for (int b = 0; b < count && !as_before; ++b) {
			const double *lie = lie_of(lies, b);
			const int o = before[static_cast<size_t>(b)];
			if (!(lie[2] > 0.5)) {
				continue;
			}
			if (o < 0) {
				content_was[static_cast<size_t>(b)] = -1.0f; // (new to the scene: nothing kept is its)
				continue;
			}
			const double *was = lie_of(scene.lies, o);
			content_was[static_cast<size_t>(b)] = static_cast<float>(was[30]);
			if (content_was[static_cast<size_t>(b)] == content_tag[static_cast<size_t>(b)]) {
				continue;
			}
			// The rows between which it differs. A place h cm up the room from its frame's origin is h / cm rows
			// above where the origin is on the screen (the room's up in its frame is its turn's own).
			const bool turned = lie[3] != 0.0;
			const double c = turned ? lie[6] : 1.0, sn = turned ? lie[7] : 0.0, px = turned ? lie[4] : 0.0, py = turned ? lie[5] : 0.0;
			const double cm = t[static_cast<size_t>(HEADER_VECS + b * BODY_VECS + FORM) * 4 + 1];
			const double origin = py + (lie[8] + 0.5 * lie[10] - px) * sn + (lie[9] + lie[11] - py) * c;
			double low = 1.0e300, high = -1.0e300;
			if (!(cm > 0.0) || std::abs(lie[12] + sn) > 1.0e-6 || std::abs(lie[13] - c) > 1.0e-6) {
				low = -1.0e300; // (not laid as this reads it: everywhere)
				high = 1.0e300;
			} else {
				// its liquid: [14] whether, [15] its level, [16] to [18] its vortex, [20] and [21] the vortex's middle
				if (was[14] != lie[14] || was[15] != lie[15] || was[16] != lie[16] || was[17] != lie[17] || was[18] != lie[18] || was[20] != lie[20] || was[21] != lie[21]) {
					const bool both = was[14] > 0.5 && lie[14] > 0.5;
					low = both ? std::min(was[15] - std::abs(was[16]), lie[15] - std::abs(lie[16])) : -1.0e300;
					high = std::max(was[14] > 0.5 ? was[15] : -1.0e300, lie[14] > 0.5 ? lie[15] : -1.0e300);
				}
				// its sediment: [19] whether, [22] its level, [23] to [27] its pile
				if (was[19] != lie[19] || was[22] != lie[22] || was[23] != lie[23] || was[24] != lie[24] || was[25] != lie[25] || was[26] != lie[26] || was[27] != lie[27]) {
					const bool both = was[19] > 0.5 && lie[19] > 0.5;
					low = std::min(low, both ? std::min(was[22], lie[22]) : -1.0e300);
					high = std::max(high, std::max(was[19] > 0.5 ? was[22] + std::abs(was[23]) : -1.0e300, lie[19] > 0.5 ? lie[22] + std::abs(lie[23]) : -1.0e300));
				}
			}
			// (Stage 65) the rows between its liquid's surface row as it was and as it is
			const double form_was = scene.table[static_cast<size_t>(HEADER_VECS + o * BODY_VECS + FORM) * 4 + 2];
			const double form_now = t[static_cast<size_t>(HEADER_VECS + b * BODY_VECS + FORM) * 4 + 2];
			double rows_lo = 1.0e30, rows_hi = -1.0e30;
			if (form_was != form_now) {
				rows_lo = std::min(form_was, form_now) - 0.02;
				rows_hi = std::max(form_was, form_now) + 0.02;
			}
			if (high < low) {
				zone_lo[static_cast<size_t>(b)] = rows_lo;
				zone_hi[static_cast<size_t>(b)] = rows_hi;
			}
			if (high >= low) {
				zone_lo[static_cast<size_t>(b)] = std::min(std::max(origin - high / std::max(cm, 1.0e-12) - 0.02, -1.0e30), rows_lo);
				zone_hi[static_cast<size_t>(b)] = std::max(std::min(origin - low / std::max(cm, 1.0e-12) + 0.02, 1.0e30), rows_hi);
			}
		}
		for (int b = 0; b < count; ++b) {
			double *lie = &lies[static_cast<size_t>(b) * SHAPE_BLOCK * 4];
			if (as_before) {
				// (the scene composed again with nothing in it changed: what changed before it stands)
				const double *was = lie_of(scene.lies, b);
				lie[28] = shape_tag[static_cast<size_t>(b)];
				for (int k = 29; k < 40; ++k) {
					lie[k] = was[k];
				}
				// (what stands behind it may have moved: its signature of their shapes is made again below)
			}
			const float *rect = &t[static_cast<size_t>(HEADER_VECS + b * BODY_VECS + RECT) * 4];
			const float layer = t[static_cast<size_t>(HEADER_VECS + b * BODY_VECS + REFRACT) * 4 + 1];
			bool lower = false;
			uint64_t h = 1469598103934665603ull, now = h, was = h;
			double low = 1.0e30, high = -1.0e30;
			const bool all = mirror(b);
			for (int o = 0; o < count; ++o) {
				const float *other = &t[static_cast<size_t>(HEADER_VECS + o * BODY_VECS + RECT) * 4];
				if (all ? o != b : t[static_cast<size_t>(HEADER_VECS + o * BODY_VECS + REFRACT) * 4 + 1] < layer && other[1] < rect[1] + rect[3] + 1.0f && other[1] + other[3] + 1.0f > rect[1]) {
					if (all) {
						h = fnv(other, 4 * sizeof(float), h); // (a mirror's: where each stands, not only its shape)
					}
					lower = true;
					h = fnv(&shape_tag[static_cast<size_t>(o)], sizeof(float), h);
					now = fnv(&content_tag[static_cast<size_t>(o)], sizeof(float), now);
					was = fnv(&content_was[static_cast<size_t>(o)], sizeof(float), was);
					low = std::min(low, zone_lo[static_cast<size_t>(o)]);
					high = std::max(high, zone_hi[static_cast<size_t>(o)]);
				}
			}
			lie[28] = shape_tag[static_cast<size_t>(b)];
			lie[29] = lower ? tag(h) : 0.0;
			if (as_before) {
				continue;
			}
			lie[30] = content_tag[static_cast<size_t>(b)];
			lie[31] = content_was[static_cast<size_t>(b)];
			lie[32] = zone_lo[static_cast<size_t>(b)];
			lie[33] = zone_hi[static_cast<size_t>(b)];
			lie[34] = lower ? tag(now) : 0.0;
			lie[35] = lower ? tag(was) : 0.0;
			lie[36] = low;
			lie[37] = high;
		}
	}
	// the screen's squares: each the bodies that reach into it, in painting order, the nearest kept
	for (int b = 0; b < count; ++b) {
		const float *rect = &t[static_cast<size_t>(HEADER_VECS + b * BODY_VECS + RECT) * 4];
		const double side = t[5 * 4 + 2], ox = t[5 * 4], oy = t[5 * 4 + 1];
		const auto square = [&](double v, double origin, int most) { return std::min(std::max(static_cast<int>(std::floor((v - origin) / side)), 0), most - 1); };
		const int c0 = square(rect[0] - 1.5, ox, GRID_COLS), c1 = square(static_cast<double>(rect[0]) + rect[2] + 1.5, ox, GRID_COLS);
		const int r0 = square(rect[1] - 1.5, oy, GRID_ROWS), r1 = square(static_cast<double>(rect[1]) + rect[3] + 1.5, oy, GRID_ROWS);
		for (int r = r0; r <= r1; ++r) {
			for (int c = c0; c <= c1; ++c) {
				float *cell = &t[static_cast<size_t>(GRID_BASE + GRID_VECS * (r * GRID_COLS + c)) * 4];
				int n = static_cast<int>(cell[0]);
				if (n == GRID_SLOTS) {
					for (int k = 1; k < GRID_SLOTS; ++k) {
						cell[k] = cell[k + 1]; // the furthest goes
					}
					--n;
				}
				cell[1 + n] = static_cast<float>(b);
				cell[0] = static_cast<float>(n + 1);
			}
		}
	}
	scene.lies = std::move(lies);
	// the long-wave rows after the visible ones (Stage 46), and where they begin
	put(0, 3, static_cast<double>(rows.size()));
	rows.insert(rows.end(), lwir.begin(), lwir.end());
	if (t != scene.table || rows != scene.rows || lines != scene.lines) {
		scene.table = std::move(t);
		scene.rows = std::move(rows);
		scene.lines = std::move(lines);
		scene.revision = next_revision();
	}
	optics_shapes(scene);
}

// The shapes (GAME_SPEC §12.3, Stage 29): [b], b < MAX_BODIES: where body b's shape begins (x), or -1 (it bends
// nothing); then each shape once: [s] its patches (x) and how far from its axis it reaches (y), and six vec4s a patch: a and c; l and whether it is a
// torus; its heights and its rings (lo.y, hi.y, rho_lo, rho_hi); its box across and in depth (lo.x, hi.x, lo.z,
// hi.z); a torus's major, cy, minor, t_lo; its t_hi, the medium below it and the one above.
void optics_shapes(OpticsScene &scene) {
	if (scene.table.size() != static_cast<size_t>(SIZE_VECS) * 4) {
		return;
	}
	const std::vector<float> &t = scene.table;
	const int count = static_cast<int>(t[0]);
	constexpr size_t HEAD = static_cast<size_t>(MAX_BODIES) * SHAPE_BLOCK * 4;
	std::vector<double> out(HEAD, 0.0);
	if (scene.lies.size() == HEAD) {
		out = scene.lies;
	}
	for (int b = 0; b < MAX_BODIES; ++b) {
		double *block = &out[static_cast<size_t>(b) * SHAPE_BLOCK * 4];
		block[0] = -1.0;
		if (block[6] == 0.0 && block[7] == 0.0) {
			block[6] = 1.0; // (a body laid out by no compose: upright)
			block[13] = 1.0;
		}
	}
	const auto capped = [](double v) { return std::min(std::max(v, -1.0e30), 1.0e30); };
	const auto put = [&](const trace::Shape &shape) {
		const double at = static_cast<double>(out.size() / 4);
		// Each patch's heights and rings as the pass culls by them (Stage 33): where the truth's patch leaves one
		// open because its surface already says it (a flat ring's height; a cone's rings between its heights), it
		// is given here, a hundredth of a millimetre wide of the surface, so that a ray tries only the surfaces
		// at its own heights and distance from the axis. Nothing the truth's bounds let through is ruled out.
		constexpr double WIDE = 1.0e-3;
		struct Bounds {
			double y_lo, y_hi, rho_lo, rho_hi;
		};
		std::vector<Bounds> bounds;
		double reach = 0.0; // how far from its axis the shape reaches
		for (const trace::Patch &p : shape.patches) {
			Bounds b{ p.lo.y, p.hi.y, p.rho_lo, p.rho_hi };
			const bool about_axis = !p.torus && p.l.x == 0.0 && p.l.z == 0.0 && p.a.x == p.a.z;
			if (about_axis && p.a.x == 0.0 && p.a.y == 0.0 && p.l.y != 0.0) {
				// a flat ring: y = -c / l.y
				b.y_lo = std::max(b.y_lo, -p.c / p.l.y - WIDE);
				b.y_hi = std::min(b.y_hi, -p.c / p.l.y + WIDE);
			} else if (about_axis && p.a.x > 0.0 && p.rho_lo <= 0.0 && p.rho_hi > 1.0e29 && ((p.lo.y > -1.0e29 && p.hi.y < 1.0e29) || (p.a.y == 0.0 && p.l.y == 0.0))) {
				// turned about the axis: rho^2 = -(a.y y^2 + l.y y + c) / a.x, between its heights
				const auto rho2 = [&](double y) { return -(p.a.y * y * y + p.l.y * y + p.c) / p.a.x; };
				const bool level = p.a.y == 0.0 && p.l.y == 0.0;
				double least = level ? rho2(0.0) : std::min(rho2(p.lo.y), rho2(p.hi.y)), most = level ? rho2(0.0) : std::max(rho2(p.lo.y), rho2(p.hi.y));
				const double turn = p.a.y != 0.0 ? -p.l.y / (2.0 * p.a.y) : 0.0;
				if (!level && p.a.y != 0.0 && turn > p.lo.y && turn < p.hi.y) {
					least = std::min(least, rho2(turn));
					most = std::max(most, rho2(turn));
				}
				b.rho_lo = std::max(0.0, std::sqrt(std::max(least, 0.0)) - WIDE);
				b.rho_hi = std::sqrt(std::max(most, 0.0)) + WIDE;
				// (a cone's other nappe lies within the same rings: where rho would be below zero it is its mirror)
				if (least < 0.0) {
					b.rho_lo = 0.0;
				}
			}
			double r = b.rho_hi;
			if (r > 1.0e29 && std::abs(p.lo.x) < 1.0e29 && std::abs(p.hi.x) < 1.0e29 && std::abs(p.lo.z) < 1.0e29 && std::abs(p.hi.z) < 1.0e29) {
				r = std::hypot(std::max(std::abs(p.lo.x), std::abs(p.hi.x)), std::max(std::abs(p.lo.z), std::abs(p.hi.z)));
			}
			reach = std::max(reach, r);
			bounds.push_back(b);
		}
		reach = reach < 1.0e29 ? reach * (1.0 + 1.0e-6) + 1.0e-4 : 1.0e30;
		// Its outline (Stage 33): a shape turned about its axis is seen from the side as its profile, so a pixel
		// beyond the profile is not the body's, and follows no ray. The furthest from the axis any surface comes
		// in each of OUTLINE_BANDS bands of its height, a hundredth of a millimetre wide; none (the heights both
		// 0) for a shape with a surface not about the axis, or one without an end.
		double foot = 1.0e300, top = -1.0e300;
		bool outlined = !shape.patches.empty();
		for (size_t i = 0; i < shape.patches.size(); ++i) {
			const trace::Patch &p = shape.patches[i];
			const bool about_axis = p.torus || (p.l.x == 0.0 && p.l.z == 0.0 && p.a.x == p.a.z);
			outlined = outlined && about_axis && bounds[i].y_lo > -1.0e29 && bounds[i].y_hi < 1.0e29 && (p.torus || p.a.x != 0.0 || bounds[i].rho_hi < 1.0e29);
			foot = std::min(foot, bounds[i].y_lo);
			top = std::max(top, bounds[i].y_hi);
		}
		outlined = outlined && top > foot;
		double outline[OUTLINE_BANDS] = {};
		for (int band = 0; outlined && band < OUTLINE_BANDS; ++band) {
			const double y0 = foot + (top - foot) * band / OUTLINE_BANDS - WIDE, y1 = foot + (top - foot) * (band + 1) / OUTLINE_BANDS + WIDE;
			for (size_t i = 0; i < shape.patches.size(); ++i) {
				const trace::Patch &p = shape.patches[i];
				const double lo = std::max(y0, bounds[i].y_lo), hi = std::min(y1, bounds[i].y_hi);
				if (lo > hi) {
					continue;
				}
				double most = bounds[i].rho_hi;
				if (p.torus) {
					const double off = p.cy < lo ? lo - p.cy : (p.cy > hi ? p.cy - hi : 0.0);
					most = std::min(most, p.major + std::sqrt(std::max(p.minor * p.minor - off * off, 0.0)));
				} else if (p.a.x != 0.0) {
					const auto rho2 = [&](double y) { return -(p.a.y * y * y + p.l.y * y + p.c) / p.a.x; };
					double most2 = std::max(rho2(lo), rho2(hi));
					const double turn = p.a.y != 0.0 ? -p.l.y / (2.0 * p.a.y) : lo;
					if (turn > lo && turn < hi) {
						most2 = std::max(most2, rho2(turn));
					}
					most = std::min(most, std::sqrt(std::max(most2, 0.0)));
				}
				outline[band] = std::max(outline[band], most + WIDE);
			}
		}
		out.insert(out.end(), { static_cast<double>(shape.patches.size()), reach, outlined ? foot : 0.0, outlined ? top : 0.0 });
		size_t k = 0;
		for (const trace::Patch &p : shape.patches) {
			const Bounds &b = bounds[k++];
			out.insert(out.end(), { capped(p.a.x), capped(p.a.y), capped(p.a.z), capped(p.c), capped(p.l.x), capped(p.l.y), capped(p.l.z), p.torus ? 1.0 : 0.0, capped(b.y_lo), capped(b.y_hi),
										  capped(b.rho_lo), capped(b.rho_hi), capped(p.lo.x), capped(p.hi.x), capped(p.lo.z), capped(p.hi.z), capped(p.major), capped(p.cy), capped(p.minor), capped(p.t_lo),
										  capped(p.t_hi), static_cast<double>(p.below), static_cast<double>(p.above), 0.0 });
		}
		for (const double v : outline) {
			out.push_back(capped(v));
		}
		return at;
	};
	std::map<const glass::Vessel *, double> made_at;
	for (int b = 0; b < count && b < MAX_BODIES; ++b) {
		const size_t at = static_cast<size_t>(HEADER_VECS + b * BODY_VECS) * 4;
		const float *rect = &t[at + RECT * 4], *form = &t[at + FORM * 4], *more = &t[at + MORE * 4], *glow = &t[at + GLOW * 4], *refract = &t[at + REFRACT * 4];
		const bool bends = refract[3] > 0.5f && (form[0] < 0.5f || form[0] > 1.5f) && form[0] < 2.5f;
		if (!bends) {
			continue;
		}
		const glass::Vessel *made = static_cast<size_t>(b) < scene.vessels.size() ? scene.vessels[static_cast<size_t>(b)] : nullptr;
		double where = -1.0;
		if (made != nullptr) {
			const auto found = made_at.find(made);
			where = found != made_at.end() ? found->second : (made_at[made] = put(trace::revolved_of(*made)));
		} else if (form[0] > 1.5f) {
			const double corners[3][2] = { { more[0], more[1] }, { more[2], more[3] }, { glow[0], glow[1] } };
			where = put(trace::prism(corners));
		} else {
			where = put(trace::cylinder(static_cast<double>(rect[2]) * 0.5 * form[1], refract[0]));
		}
		out[static_cast<size_t>(b) * SHAPE_BLOCK * 4] = where;
		out[static_cast<size_t>(b) * SHAPE_BLOCK * 4 + 1] = made != nullptr ? 1.0 : 0.0;
		if (out[static_cast<size_t>(b) * SHAPE_BLOCK * 4 + 10] == 0.0) {
			// (a scene whose table was set by hand: it stands as its rectangle says)
			for (int k = 0; k < 4; ++k) {
				out[static_cast<size_t>(b) * SHAPE_BLOCK * 4 + 8 + static_cast<size_t>(k)] = rect[k];
			}
		}
	}
	if (out != scene.shapes_exact) {
		scene.shapes.assign(out.size(), 0.0f);
		for (size_t i = 0; i < out.size(); ++i) {
			scene.shapes[i] = static_cast<float>(out[i]);
		}
		scene.shapes_exact = std::move(out);
		scene.shapes_revision = next_revision();
	}
}

int optics_body(const OpticsScene &scene, const std::string &id) {
	if (scene.table.empty()) {
		return -1;
	}
	const float wanted = tag(fnv(id.data(), id.size()));
	const int count = static_cast<int>(scene.table[0]);
	for (int b = 0; b < count; ++b) {
		if (scene.table[static_cast<size_t>(HEADER_VECS + b * BODY_VECS + IDS) * 4] == wanted) {
			return b;
		}
	}
	return -1;
}

// The wall as a table (GAME_SPEC §12.3; lab/optics_scene.hpp lays it out).
namespace {
constexpr double FAR_OFF = 50000.0; // (pixels: half as far as a ray that never reaches the wall is put)
} // namespace

void optics_wall(OpticsScene &scene, const OpticsRoom &room) {
	scene.drawn.clear();
	// the breakpoints: every edge of the bench and of what is drawn, across and down
	// (and far off to each side, FAR_OFF: beyond it is where a ray that never reaches the wall is put)
	std::vector<double> xs{ room.x, static_cast<double>(room.x) + room.w, -FAR_OFF, FAR_OFF }, ys{ room.y, static_cast<double>(room.y) + room.h };
	if (room.beyond >= 0.0) {
		xs.push_back(0.0); // (the screen's own edges: beyond them is the endless plane)
		xs.push_back(static_cast<double>(WALL_COLUMNS));
	}
	for (const OpticsShape &sh : room.shapes) {
		if (!(sh.w > 0.0f) || !(sh.h > 0.0f)) {
			continue;
		}
		xs.push_back(sh.x);
		xs.push_back(static_cast<double>(sh.x) + sh.w);
		ys.push_back(sh.y);
		ys.push_back(static_cast<double>(sh.y) + sh.h);
		for (const double v : { static_cast<double>(sh.x), static_cast<double>(sh.y), static_cast<double>(sh.w), static_cast<double>(sh.h), sh.radiance }) {
			scene.drawn.push_back(static_cast<float>(v));
		}
	}
	for (std::vector<double> *v : { &xs, &ys }) {
		std::sort(v->begin(), v->end());
		v->erase(std::unique(v->begin(), v->end()), v->end());
	}
	const int m = static_cast<int>(xs.size()), my = static_cast<int>(ys.size());
	// cell j across is from breakpoint j - 1 to breakpoint j; painted in order, the topmost shape's radiance
	const auto cells_of = [](const std::vector<double> &v, double lo, double hi, int &from, int &to) {
		from = static_cast<int>(std::lower_bound(v.begin(), v.end(), lo) - v.begin()) + 1;
		to = static_cast<int>(std::lower_bound(v.begin(), v.end(), hi) - v.begin());
	};
	std::vector<float> grid(static_cast<size_t>(my + 1) * static_cast<size_t>(m + 1), static_cast<float>(room.elsewhere));
	const auto paint = [&](double x, double y, double w, double h, double radiance) {
		int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
		cells_of(xs, x, x + w, x0, x1);
		cells_of(ys, y, y + h, y0, y1);
		for (int r = y0; r <= y1; ++r) {
			std::fill(grid.begin() + static_cast<std::ptrdiff_t>(r) * (m + 1) + x0, grid.begin() + static_cast<std::ptrdiff_t>(r) * (m + 1) + x1 + 1, static_cast<float>(radiance));
		}
	};
	if (room.w > 0.0f && room.h > 0.0f) {
		paint(room.x, room.y, room.w, room.h, room.bench);
	}
	for (const OpticsShape &sh : room.shapes) {
		if (sh.w > 0.0f && sh.h > 0.0f) {
			paint(sh.x, sh.y, sh.w, sh.h, sh.radiance);
		}
	}
	// (in a room: far to the left is where a ray that comes to a black cell of a face is put, far to the right
	// one that comes to any other cell off the wall; with no room, both are the room in front)
	const double far_left = room.cube > 0.0 ? room.edge : room.front, far_right = room.cube > 0.0 && room.beyond >= 0.0 ? room.beyond : room.front;
	// A ray that leaves a body sideways or back toward the eye never reaches the wall: it is put far off to the
	// side it goes (100,000 pixels), and what it shows is the room in front of the bench, which is what glass
	// reflects (Stage 33). It read the wall's table there, which is the darkness beside the bench: grey patches
	// where a vortex or a surface turned rays back, beside reflections of a room four times as bright.
	// And the wall beyond the screen is an endless plane of one radiance, where the room says so (Stage 33, the
	// user's word: the planes behind and in front are endless and white): every cell left of the screen and right
	// of it, and (below) the rows over and under it.
	for (int r = 0; r <= my; ++r) {
		for (int j = 0; room.beyond >= 0.0 && j <= m; ++j) {
			const bool left = j < m && xs[static_cast<size_t>(j)] <= 0.0, right = j > 0 && xs[static_cast<size_t>(j - 1)] >= static_cast<double>(WALL_COLUMNS);
			if (left || right) {
				grid[static_cast<size_t>(r) * static_cast<size_t>(m + 1) + static_cast<size_t>(j)] = static_cast<float>(room.beyond);
			}
		}
		grid[static_cast<size_t>(r) * static_cast<size_t>(m + 1)] = static_cast<float>(far_left);
		grid[static_cast<size_t>(r) * static_cast<size_t>(m + 1) + static_cast<size_t>(m)] = static_cast<float>(far_right);
	}
	scene.far_left = far_left;
	scene.far_right = far_right;
	scene.wall_beyond = room.beyond;
	// each row of pixels: the cells down that it crosses, each by its share of the row's height
	std::vector<std::vector<float>> profiles;
	std::map<std::vector<float>, int> known;
	std::vector<int> row_profile(static_cast<size_t>(WALL_ROWS + 2), 0); // (from the row over the screen to the row under it)
	std::vector<float> blend(static_cast<size_t>(m + 1));
	for (int y = -1; y <= WALL_ROWS; ++y) {
		std::fill(blend.begin(), blend.end(), 0.0f);
		const int first = static_cast<int>(std::upper_bound(ys.begin(), ys.end(), static_cast<double>(y)) - ys.begin());
		for (int r = first; r <= my; ++r) {
			const double lo = std::max(static_cast<double>(y), r == 0 ? -1.0e300 : ys[static_cast<size_t>(r - 1)]), hi = std::min(y + 1.0, r == my ? 1.0e300 : ys[static_cast<size_t>(r)]);
			if (hi <= lo) {
				break;
			}
			for (int j = 0; j <= m; ++j) {
				blend[static_cast<size_t>(j)] += static_cast<float>((hi - lo) * grid[static_cast<size_t>(r) * static_cast<size_t>(m + 1) + static_cast<size_t>(j)]);
			}
		}
		if (room.beyond >= 0.0 && (y == -1 || y == WALL_ROWS)) {
			// the row over the screen and the row under it, which every row beyond them is read as: the endless plane
			std::fill(blend.begin(), blend.end(), static_cast<float>(room.beyond));
			blend[0] = static_cast<float>(far_left);
			blend[static_cast<size_t>(m)] = static_cast<float>(far_right);
		}
		const auto found = known.emplace(blend, static_cast<int>(profiles.size()));
		if (found.second) {
			profiles.push_back(blend);
		}
		row_profile[static_cast<size_t>(y + 1)] = found.first->second;
	}
	std::vector<float> &w = scene.wall;
	const size_t cells = static_cast<size_t>(WALL_BREAK_BASE + m);
	w.assign((cells + profiles.size() * static_cast<size_t>(m + 1)) * 4, 0.0f);
	w[0] = static_cast<float>(m);
	w[1] = static_cast<float>(profiles.size());
	for (int y = -1; y <= WALL_ROWS; ++y) {
		w[static_cast<size_t>(WALL_ROW_BASE + y) * 4] = static_cast<float>(row_profile[static_cast<size_t>(y + 1)]);
	}
	for (int c = 0; c < WALL_COLUMNS; ++c) {
		w[static_cast<size_t>(WALL_COLUMN_BASE + c) * 4] = static_cast<float>(std::upper_bound(xs.begin(), xs.end(), static_cast<double>(c)) - xs.begin());
	}
	for (int j = 0; j < m; ++j) {
		w[static_cast<size_t>(WALL_BREAK_BASE + j) * 4] = static_cast<float>(xs[static_cast<size_t>(j)]);
	}
	for (size_t p = 0; p < profiles.size(); ++p) {
		double integral = 0.0; // from breakpoint 0 to the cell's left end
		for (int j = 0; j <= m; ++j) {
			float *cell = &w[(cells + p * static_cast<size_t>(m + 1) + static_cast<size_t>(j)) * 4];
			if (j >= 2) {
				// the breakpoints as the table holds them, in float: the integral is of what the renderer reads
				integral += static_cast<double>(profiles[p][static_cast<size_t>(j - 1)]) * (static_cast<double>(static_cast<float>(xs[static_cast<size_t>(j - 1)])) - static_cast<double>(static_cast<float>(xs[static_cast<size_t>(j - 2)])));
			}
			const double coarse = std::round(integral * 64.0) / 64.0;
			cell[0] = profiles[p][static_cast<size_t>(j)];
			cell[1] = static_cast<float>(coarse);
			cell[2] = static_cast<float>(integral - coarse);
		}
	}
	scene.wall_revision = next_revision();
}

} // namespace lab
