// The renderer's table, from the state (GAME_SPEC §12).
#include "lab/optics_scene.hpp"

#include "chem/optics.hpp"
#include "chem/species.hpp"
#include "lab/colour.hpp"

#include <array>
#include <cmath>
#include <map>

namespace lab {

namespace {

using namespace optics_layout;
using chem::optics::Kind;
using chem::optics::Spectrum;
using BandValues = std::array<double, BANDS>;

static_assert(BANDS == chem::optics::GAME_BANDS, "the table carries the game's bands");

constexpr double PI = 3.14159265358979323846;
constexpr double LN10 = 2.302585092994046;
constexpr double REPOSE = 0.7; // a heap's slope: tan 35 degrees, a dry powder's angle of repose
constexpr double PACKING = 0.5; // how much of a heap is solid
constexpr double EXTINCTION_EFFICIENCY = 2.0; // of a particle much larger than the wavelength
constexpr double GREY = 0.5; // the reflectance of a solid with no spectrum

// What a species does to light, by band.
struct Medium {
	Kind kind = Kind::None;
	BandValues band{}; // Absorb: optical depth per mol/L per cm. Reflect: reflectance. Transmit: optical depth of the reference thickness
};

const Medium &medium(const std::string &species) {
	static std::map<std::string, Medium> known;
	const auto found = known.find(species);
	if (found != known.end()) {
		return found->second;
	}
	Medium m;
	const chem::SpeciesId id = chem::builtin_species().find(species);
	if (id.index >= 0 && (m.kind = chem::optics::kind(id)) != Kind::None) {
		Spectrum linear = chem::optics::spectrum(id);
		for (double &v : linear) {
			v = m.kind == Kind::Absorb ? v * LN10 : m.kind == Kind::Transmit ? -std::log(v) : v;
		}
		chem::optics::band_averages(chem::optics::game_bands(), linear, m.band);
	}
	return known.emplace(species, m).first->second;
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
	std::vector<float> t(static_cast<size_t>(SIZE_VECS) * 4, 0.0f);
	const auto put = [&](int vec, int part, double v) { t[static_cast<size_t>(vec) * 4 + static_cast<size_t>(part)] = static_cast<float>(v); };
	const auto put_bands = [&](int vec, const BandValues &v) {
		for (int k = 0; k < BANDS; ++k) {
			put(vec + k / 4, k % 4, v[static_cast<size_t>(k)]);
		}
	};
	put(0, 1, room.bench);
	put(0, 2, room.elsewhere);
	put(1, 0, room.x);
	put(1, 1, room.y);
	put(1, 2, room.w);
	put(1, 3, room.h);
	const chem::optics::Bands &bands = chem::optics::game_bands();
	for (int k = 0; k < BANDS; ++k) {
		double rgb[3];
		to_linear_srgb(bands.lit[static_cast<size_t>(k)], rgb);
		for (int c = 0; c < 3; ++c) {
			put(2 + k, c, rgb[c]);
		}
	}

	int count = 0;
	for (const OpticsBody &body : bodies) {
		if (count == MAX_BODIES || body.item == nullptr || !(body.w > 0.0f) || !(body.h > 0.0f)) {
			continue;
		}
		const ItemView &v = *body.item;
		const int at = HEADER_VECS + count * BODY_VECS;
		++count;
		const bool ball = v.kind == "balloon";
		const double bottom = static_cast<double>(body.y) + body.h;
		put(at + RECT, 0, body.x);
		put(at + RECT, 1, body.y);
		put(at + RECT, 2, body.w);
		put(at + RECT, 3, body.h);
		put(at + FORM, 0, ball ? 1.0 : 0.0);
		put(at + FORM, 2, bottom + 2.0); // no liquid: its surface is under the floor

		// ---- what is in it
		BandValues liquid{}, solid{}, wall{};
		Spectrum reflectance{};
		double solid_g = 0.0, cross_section_cm2 = 0.0;
		for (const SubstanceView &s : v.contents) {
			const Medium &m = medium(s.species);
			if (s.wall) {
				if (m.kind == Kind::Transmit) {
					wall = m.band;
				}
				continue;
			}
			if (m.kind == Kind::Absorb && v.liquid_ml > 0.0) {
				const double molar = s.mol / (v.liquid_ml / 1000.0);
				for (int k = 0; k < BANDS; ++k) {
					liquid[static_cast<size_t>(k)] += m.band[static_cast<size_t>(k)] * molar;
				}
			}
			if (s.phase == 's' && s.g > 0.0) {
				const chem::SpeciesId id = chem::builtin_species().find(s.species);
				for (int k = 0; k < BANDS; ++k) {
					solid[static_cast<size_t>(k)] += s.g * (m.kind == Kind::Reflect ? m.band[static_cast<size_t>(k)] : GREY);
				}
				for (size_t i = 0; i < reflectance.size(); ++i) {
					reflectance[i] += s.g * (m.kind == Kind::Reflect ? chem::optics::spectrum(id)[i] : GREY);
				}
				solid_g += s.g;
				const double cm = s.size_um * 1e-4;
				cross_section_cm2 += static_cast<double>(s.particles) * PI * cm * cm / 4.0;
			}
		}
		if (solid_g > 0.0) { // a mixture of powders: by mass
			for (double &r : solid) {
				r /= solid_g;
			}
			for (double &r : reflectance) {
				r /= solid_g;
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
		put_bands(at + LIQUID, liquid);
		put_bands(at + SOLID, solid);
		put_bands(at + WALL, wall);
	}
	put(0, 0, count);
	if (t != scene.table) {
		scene.table = std::move(t);
		++scene.revision;
	}
}

} // namespace lab
