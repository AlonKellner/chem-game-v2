// The renderer's table, from the state (GAME_SPEC §12).
#include "lab/optics_scene.hpp"

#include "chem/optics.hpp"
#include "chem/species.hpp"
#include "lab/colour.hpp"

#include <cmath>

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
		++count;
		const bool ball = v.kind == "balloon";
		const double bottom = static_cast<double>(body.y) + body.h;
		put(at + RECT, 0, body.x);
		put(at + RECT, 1, body.y);
		put(at + RECT, 2, body.w);
		put(at + RECT, 3, body.h);
		put(at + FORM, 0, ball ? 1.0 : 0.0);
		put(at + FORM, 2, bottom + 2.0); // no liquid: its surface is under the floor

		// ---- what is in it, at 1 nm, and its liquid's narrow lines
		Spectrum reflectance{};
		double solid_g = 0.0, cross_section_cm2 = 0.0;
		const int first_line = line_count;
		for (const SubstanceView &s : v.contents) {
			const chem::SpeciesId id = chem::builtin_species().find(s.species);
			const Kind kind = id.index >= 0 ? chem::optics::kind(id) : Kind::None;
			if (s.wall) {
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
		// which body this is, and a signature of what it holds: a pixel's history follows them (GAME_SPEC §12.2)
		put(at + IDS, 0, tag(fnv(v.id.data(), v.id.size())));
		uint64_t h = fnv(liquid, ROWS_PER_BODY * ROW * sizeof(float));
		h = fnv(&lines[static_cast<size_t>(first_line) * 4], static_cast<size_t>(line_count - first_line) * 4 * sizeof(float), h);
		put(at + IDS, 1, tag(h));
		put(at + IDS, 2, first_line);
		put(at + IDS, 3, line_count - first_line);
	}
	put(0, 0, count);

	if (scene.lights.empty()) {
		const chem::optics::EnergyTable &room_light = chem::optics::room_light();
		scene.lights.resize(static_cast<size_t>(LIGHTS * LIGHT_ENTRIES) * 4);
		for (size_t j = 0; j < static_cast<size_t>(LIGHT_ENTRIES); ++j) {
			const chem::optics::EnergyEntry &e = room_light.entry[j];
			scene.lights[4 * j] = static_cast<float>(e.nm - chem::optics::FIRST_NM);
			scene.lights[4 * j + 1] = static_cast<float>(e.up_to.x);
			scene.lights[4 * j + 2] = static_cast<float>(e.up_to.y);
			scene.lights[4 * j + 3] = static_cast<float>(e.up_to.z);
		}
		++scene.revision;
	}
	if (t != scene.table || rows != scene.rows || lines != scene.lines) {
		scene.table = std::move(t);
		scene.rows = std::move(rows);
		scene.lines = std::move(lines);
		++scene.revision;
	}
}

} // namespace lab
