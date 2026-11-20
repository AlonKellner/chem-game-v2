// The renderer's table, from the state (GAME_SPEC §12).
#include "lab/optics_scene.hpp"

#include "chem/optics.hpp"
#include "chem/species.hpp"
#include "lab/colour.hpp"
#include "lab/optics_trace.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace lab {

namespace {

using namespace optics_layout;
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

// the depth of liquid of volume `ml` at the bottom of a ball of radius `r` cm
double pool_depth(double ml, double r) {
	double lo = 0.0, hi = 2.0 * r;
	for (int i = 0; i < 40; ++i) {
		const double h = (lo + hi) / 2.0;
		(PI * h * h * (3.0 * r - h) / 3.0 < ml ? lo : hi) = h;
	}
	return (lo + hi) / 2.0;
}

} // namespace

namespace {

// (in `t`, the scene's table being made: each body's MAP and the tag its pixels' histories follow)
void place_maps(OpticsScene &scene, std::vector<float> &t, const std::vector<float> &rows, int count, double wall_cm) {
	const auto put = [&](int vec, int part, double v) { t[static_cast<size_t>(vec) * 4 + static_cast<size_t>(part)] = static_cast<float>(v); };
	// Each refracting body's map: traced again only when its shape, its glass or its indices change; where
	// it stands and what fills it change nothing
	scene.map_keys.resize(static_cast<size_t>(MAX_BODIES), 0);
	scene.map_blocks.resize(static_cast<size_t>(MAX_BODIES));
	bool maps_changed = false;
	std::vector<int> offsets(static_cast<size_t>(MAX_BODIES), -1);
	int map_at = 0; // vec4s
	for (int b = 0; b < MAX_BODIES; ++b) {
		const int at = HEADER_VECS + b * BODY_VECS;
		const float *rect = &t[static_cast<size_t>(at + RECT) * 4], *form = &t[static_cast<size_t>(at + FORM) * 4], *refract = &t[static_cast<size_t>(at + REFRACT) * 4];
		const bool refracts = b < count && refract[3] > 0.5f && (form[0] < 0.5f || form[0] > 1.5f);
		uint64_t key = 0;
		trace::MapSpec spec;
		if (refracts) {
			const bool prism = form[0] > 1.5f;
			const double axis = rect[0] + rect[2] * 0.5 + (prism ? t[static_cast<size_t>(at + GLOW) * 4 + 2] : 0.0f);
			spec.cm_per_pixel = form[1];
			spec.left = rect[0] - 1.0 - axis;
			spec.right = rect[0] + rect[2] + 1.0 - axis;
			spec.grid = std::floor(axis) - axis; // the screen's pixels, from its axis
			spec.wall_cm = wall_cm;
			spec.glass_index = &rows[static_cast<size_t>((b * ROWS_PER_BODY + WALL_INDEX_ROW) * ROW)];
			if (prism) {
				const float *more = &t[static_cast<size_t>(at + MORE) * 4], *glow = &t[static_cast<size_t>(at + GLOW) * 4];
				const double corners[3][2] = { { more[0], more[1] }, { more[2], more[3] }, { glow[0], glow[1] } };
				spec.shape = trace::prism(corners);
				spec.media = 1;
				key = fnv(more, 4 * sizeof(float), fnv(glow, 3 * sizeof(float)));
			} else {
				spec.shape = trace::cylinder(rect[2] * 0.5 * form[1], refract[0]);
				spec.media = 2;
				spec.liquid_index = &rows[static_cast<size_t>((b * ROWS_PER_BODY + LIQUID_INDEX_ROW) * ROW)];
				key = fnv(refract, sizeof(float), fnv(spec.liquid_index, ROW * sizeof(float)));
			}
			const double span[6] = { spec.cm_per_pixel, spec.left, spec.right, spec.wall_cm, static_cast<double>(rect[2]), spec.grid };
			key = fnv(span, sizeof span, fnv(spec.glass_index, ROW * sizeof(float), key)) | 1u;
		}
		std::vector<float> &block = scene.map_blocks[static_cast<size_t>(b)];
		if (scene.map_keys[static_cast<size_t>(b)] != key) {
			block = refracts ? trace::map_block(trace::build_map(spec)) : std::vector<float>();
			scene.map_keys[static_cast<size_t>(b)] = key;
			maps_changed = true;
		}
		if (refracts) {
			offsets[static_cast<size_t>(b)] = map_at;
			put(at + MAP, 0, map_at);
			put(at + MAP, 1, block[0]); // the whole pixels it spans
			put(at + MAP, 2, std::floor(spec.left - spec.grid) + spec.grid);
			put(at + MAP, 3, block[3]); // its pieces in wavelength
			map_at += static_cast<int>(block.size() / 4);
			// a pixel's history follows the map it looks through
			const float tagged = t[static_cast<size_t>(at + IDS) * 4 + 1];
			put(at + IDS, 1, tag(fnv(&key, sizeof key, fnv(&tagged, sizeof tagged))));
		}
	}
	if (maps_changed || static_cast<int>(scene.maps.size()) != map_at * 4) {
		scene.maps.assign(static_cast<size_t>(map_at) * 4, 0.0f);
		for (int b = 0; b < MAX_BODIES; ++b) {
			if (offsets[static_cast<size_t>(b)] >= 0) {
				std::copy(scene.map_blocks[static_cast<size_t>(b)].begin(), scene.map_blocks[static_cast<size_t>(b)].end(), scene.maps.begin() + static_cast<std::ptrdiff_t>(offsets[static_cast<size_t>(b)]) * 4);
			}
		}
		++scene.maps_revision;
	}
}

} // namespace

void optics_compose(OpticsScene &scene, const OpticsRoom &room, const std::vector<OpticsBody> &bodies) {
	std::vector<float> t(static_cast<size_t>(SIZE_VECS) * 4, 0.0f), rows(static_cast<size_t>(ROWS_SIZE), 0.0f), lines(static_cast<size_t>(MAX_LINES) * 4, 0.0f);
	const auto put = [&](int vec, int part, double v) { t[static_cast<size_t>(vec) * 4 + static_cast<size_t>(part)] = static_cast<float>(v); };
	put(0, 1, room.bench);
	put(0, 2, room.elsewhere);
	put(0, 3, LIGHTS);
	put(1, 0, room.x);
	put(1, 1, room.y);
	put(1, 2, room.w);
	put(1, 3, room.h);
	put(2, 0, room.wall_cm);
	put(2, 1, room.front);
	put(2, 2, room.refraction ? 1.0 : 0.0);
	put(2, 3, room.cuts ? 1.0 : 0.0);

	// what the wall's table is made from: the room and everything drawn on it
	uint64_t wall_key = fnv(&room.x, 4 * sizeof(float));
	wall_key = fnv(&room.bench, 2 * sizeof(double), wall_key);
	for (const OpticsShape &sh : room.shapes) {
		wall_key = fnv(&sh.x, 4 * sizeof(float), wall_key);
		wall_key = fnv(&sh.radiance, sizeof(double), wall_key);
	}
	wall_key |= 1u; // never 0: a scene with no table yet
	int count = 0, line_count = 0;
	for (const OpticsBody &body : bodies) {
		if (count == MAX_BODIES || body.item == nullptr || !(body.w > 0.0f) || !(body.h > 0.0f)) {
			continue;
		}
		const ItemView &v = *body.item;
		const int at = HEADER_VECS + count * BODY_VECS;
		float *liquid = &rows[static_cast<size_t>((count * ROWS_PER_BODY + LIQUID_ROW) * ROW)];
		float *solid = &rows[static_cast<size_t>((count * ROWS_PER_BODY + SOLID_ROW) * ROW)];
		float *wall = &rows[static_cast<size_t>((count * ROWS_PER_BODY + WALL_ROW) * ROW)];
		float *liquid_index = &rows[static_cast<size_t>((count * ROWS_PER_BODY + LIQUID_INDEX_ROW) * ROW)];
		float *wall_index = &rows[static_cast<size_t>((count * ROWS_PER_BODY + WALL_INDEX_ROW) * ROW)];
		for (int i = 0; i < ROW; ++i) {
			liquid_index[i] = wall_index[i] = 1.0f; // air
		}
		++count;
		const bool ball = v.kind == "balloon";
		const double bottom = static_cast<double>(body.y) + body.h;
		put(at + RECT, 0, body.x);
		put(at + RECT, 1, body.y);
		put(at + RECT, 2, body.w);
		put(at + RECT, 3, body.h);
		put(at + FORM, 0, ball ? 1.0 : 0.0);
		put(at + FORM, 2, bottom + 2.0); // no liquid: its surface is under the floor
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
			put(at + REFRACT, 3, room.refraction ? 1.0 : 0.0);
			put(at + IDS, 0, tag(fnv(v.id.data(), v.id.size())));
			uint64_t h = fnv(&t[static_cast<size_t>(at) * 4], static_cast<size_t>(BODY_VECS) * 4 * sizeof(float));
			h = fnv(&wall_key, sizeof wall_key, h); // what it sees behind it
			put(at + IDS, 1, tag(h));
			continue;
		}

		// ---- what is in it, at 1 nm, and its liquid's narrow lines
		Spectrum reflectance{};
		double solid_g = 0.0, cross_section_cm2 = 0.0, wall_g = 0.0, wall_density = 0.0;
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
					wall_g += s.g;
					wall_density = chem::optics::index_density(id);
				}
				if (kind == Kind::Transmit) {
					const Spectrum &tr = chem::optics::spectrum(id);
					for (size_t i = 0; i < tr.size(); ++i) {
						wall[i] = static_cast<float>(-std::log(tr[i]));
					}
				}
				continue;
			}
			if (kind == Kind::Absorb && v.liquid_ml > 0.0 && s.phase != 's' && s.phase != 'g') {
				const double molar = s.mol / (v.liquid_ml / 1000.0);
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
		}
		const double solid_ml = v.condensed_ml > v.liquid_ml ? v.condensed_ml - v.liquid_ml : 0.0;
		if (v.liquid_ml > 0.0) {
			// a liquid's index is water's: the solutes' share is not modelled (GAME_SPEC §20)
			const Spectrum &n = chem::optics::refractive_index(chem::builtin_species().find("water"));
			for (size_t i = 0; i < n.size(); ++i) {
				liquid_index[i] = static_cast<float>(n[i]);
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
			// a cylinder of its volume and of the proportions it is drawn with
			const double aspect = static_cast<double>(body.h) / body.w;
			const double d_cm = std::cbrt(4.0 * (v.volume_ml > 0.0 ? v.volume_ml : 1.0) / (PI * aspect));
			const double cm_per_px = d_cm / body.w;
			const double floor_cm2 = PI * d_cm * d_cm / 4.0;
			put(at + FORM, 1, cm_per_px);
			if (room.refraction && wall_g > 0.0 && wall_density > 0.0) {
				// its glass: the wall's mass over its side and its floor, at most a fifth of its radius
				const double h_cm = d_cm * aspect, area = PI * d_cm * h_cm + floor_cm2;
				const double glass_cm = wall_g / wall_density / area;
				put(at + REFRACT, 0, glass_cm < 0.2 * d_cm / 2.0 ? glass_cm : 0.2 * d_cm / 2.0);
			}
			put(at + REFRACT, 3, room.refraction ? 1.0 : 0.0);
			if (v.liquid_ml > 0.0) {
				put(at + FORM, 2, bottom - v.condensed_ml / floor_cm2 / cm_per_px);
			}
			if (solid_ml > 0.0 && v.stirred && v.liquid_ml > 0.0) {
				// stirred up: every particle shades its own cross-section, twice over
				put(at + MORE, 0, EXTINCTION_EFFICIENCY * cross_section_cm2 / v.liquid_ml);
			} else if (solid_ml > 0.0) {
				// at rest: a heap at its angle of repose, a cone until it reaches the wall
				const double heap_ml = solid_ml / PACKING, r_wall = d_cm / 2.0;
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
			glow_of(v.celsius + 273.15, reflectance, rgb);
			for (int c = 0; c < 3; ++c) {
				put(at + GLOW, c, rgb[c]);
			}
		}
		// which body this is, and a signature of what it holds and sees: a pixel's history follows them (GAME_SPEC §12.2)
		put(at + IDS, 0, tag(fnv(v.id.data(), v.id.size())));
		uint64_t h = fnv(liquid, ROWS_PER_BODY * ROW * sizeof(float));
		h = fnv(&t[static_cast<size_t>(at + REFRACT) * 4], 4 * sizeof(float), h);
		h = fnv(&wall_key, sizeof wall_key, h); // what it sees behind it
		h = fnv(&lines[static_cast<size_t>(first_line) * 4], static_cast<size_t>(line_count - first_line) * 4 * sizeof(float), h);
		put(at + IDS, 1, tag(h));
		put(at + IDS, 2, first_line);
		put(at + IDS, 3, line_count - first_line);
	}
	put(0, 0, count);

	if (scene.lights.empty()) {
		const chem::optics::EnergyTable &room_light = chem::optics::room_light();
		scene.lights.assign(static_cast<size_t>(LIGHTS * LIGHT_SIZE) * 4, 0.0f);
		for (size_t j = 0; j < static_cast<size_t>(LIGHT_ENTRIES); ++j) {
			const chem::optics::EnergyEntry &e = room_light.entry[j];
			scene.lights[4 * j] = static_cast<float>(e.nm - chem::optics::FIRST_NM);
			scene.lights[4 * j + 1] = static_cast<float>(e.up_to.x);
			scene.lights[4 * j + 2] = static_cast<float>(e.up_to.y);
			scene.lights[4 * j + 3] = static_cast<float>(e.up_to.z);
		}
		// the table the other way: the share of the energy up to each whole nm (the entries ascend in both)
		size_t j = 0;
		for (int i = 0; i < ROW; ++i) {
			const double nm = i;
			while (j + 2 < static_cast<size_t>(LIGHT_ENTRIES) && scene.lights[4 * (j + 1)] <= nm) {
				++j;
			}
			const double lo = scene.lights[4 * j], hi = scene.lights[4 * (j + 1)];
			const double part = hi > lo ? std::min(std::max((nm - lo) / (hi - lo), 0.0), 1.0) : 0.0;
			scene.lights[4 * (static_cast<size_t>(LIGHT_ENTRIES) + static_cast<size_t>(i))] = static_cast<float>((static_cast<double>(j) + part) / (LIGHT_ENTRIES - 1));
		}
		++scene.revision;
	}
	// the wall's table (GAME_SPEC §12.3): made again only when what is drawn on it changes
	if (scene.wall_key != wall_key || scene.wall.empty()) {
		optics_wall(scene, room);
		scene.wall_key = wall_key;
	}
	place_maps(scene, t, rows, count, room.wall_cm);
	if (t != scene.table || rows != scene.rows || lines != scene.lines) {
		scene.table = std::move(t);
		scene.rows = std::move(rows);
		scene.lines = std::move(lines);
		++scene.revision;
	}
}

void optics_maps(OpticsScene &scene) {
	if (scene.table.size() != static_cast<size_t>(SIZE_VECS) * 4 || scene.rows.size() != static_cast<size_t>(ROWS_SIZE)) {
		return;
	}
	place_maps(scene, scene.table, scene.rows, static_cast<int>(scene.table[0]), scene.table[2 * 4]);
	++scene.revision;
}

// The wall as a table (GAME_SPEC §12.3; lab/optics_scene.hpp lays it out).
void optics_wall(OpticsScene &scene, const OpticsRoom &room) {
	scene.drawn.clear();
	// the breakpoints: every edge of the bench and of what is drawn, across and down
	std::vector<double> xs{ room.x, static_cast<double>(room.x) + room.w }, ys{ room.y, static_cast<double>(room.y) + room.h };
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
	// each row of pixels: the cells down that it crosses, each by its share of the row's height
	std::vector<std::vector<float>> profiles;
	std::map<std::vector<float>, int> known;
	std::vector<int> row_profile(static_cast<size_t>(WALL_ROWS), 0);
	std::vector<float> blend(static_cast<size_t>(m + 1));
	for (int y = 0; y < WALL_ROWS; ++y) {
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
		const auto found = known.emplace(blend, static_cast<int>(profiles.size()));
		if (found.second) {
			profiles.push_back(blend);
		}
		row_profile[static_cast<size_t>(y)] = found.first->second;
	}
	std::vector<float> &w = scene.wall;
	const size_t cells = static_cast<size_t>(WALL_BREAK_BASE + m);
	w.assign((cells + profiles.size() * static_cast<size_t>(m + 1)) * 4, 0.0f);
	w[0] = static_cast<float>(m);
	w[1] = static_cast<float>(profiles.size());
	for (int y = 0; y < WALL_ROWS; ++y) {
		w[static_cast<size_t>(WALL_ROW_BASE + y) * 4] = static_cast<float>(row_profile[static_cast<size_t>(y)]);
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
	++scene.wall_revision;
}

} // namespace lab
