// The renderer's table, from the state (GAME_SPEC §12).
#include "lab/optics_scene.hpp"
#include "lab/glassware.hpp"

#include "chem/optics.hpp"
#include "chem/species.hpp"
#include "lab/colour.hpp"
#include "lab/optics_trace.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <cmath>
#include <map>
#include <string>

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

// The same at each nanometre, as a multiple of the room's light there (GAME_SPEC §12.3, Stage 21): a light
// in the pass, which what is in front of the solid bends, absorbs and turns away as it does the wall's.
void glow_row(double kelvin, const Spectrum &reflectance, float *row) {
	if (kelvin < 500.0) {
		return;
	}
	// the room's light at each nanometre, on the scale on which its white is Y = 1
	static const double white = [] {
		double y = 0.0;
		for (size_t i = 0; i < chem::optics::d65().size(); ++i) {
			y += chem::optics::ybar()[i] * chem::optics::d65()[i];
		}
		return y;
	}();
	for (size_t i = 0; i < reflectance.size(); ++i) {
		const double metres = (chem::optics::FIRST_NM + static_cast<double>(i)) * 1e-9;
		const double radiance = 1.191042972e-16 / (metres * metres * metres * metres * metres) / (std::exp(0.014387769 / (metres * kelvin)) - 1.0) * 1e-9;
		row[i] = static_cast<float>(radiance * (1.0 - reflectance[i]) * 683.002 / ROOM_WHITE_CD_M2 / (chem::optics::d65()[i] / white));
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

// the builder's version: a map made by another is not read
constexpr uint64_t MAP_STORE_VERSION = 25006;
// the maps no body looks through that are kept (vec4s: 16 MB), lest a vessel lifted and set down again sends
// all the maps to the GPU again
constexpr int64_t MAPS_IDLE_MOST = int64_t{ 1 } << 20;

std::mutex &store_lock() {
	static std::mutex lock;
	return lock;
}
std::string &store_directory() {
	static std::string directory;
	return directory;
}
std::string store_path(uint64_t key) {
	char name[40];
	std::snprintf(name, sizeof name, "%016llx.map", static_cast<unsigned long long>(fnv(&MAP_STORE_VERSION, sizeof MAP_STORE_VERSION, key)));
	return store_directory() + "/" + name;
}

} // namespace

void optics_store(const std::string &directory) {
	const std::lock_guard<std::mutex> hold(store_lock());
	store_directory() = directory;
}

bool optics_store_read(uint64_t key, std::vector<float> &block) {
	const std::lock_guard<std::mutex> hold(store_lock());
	if (store_directory().empty()) {
		return false;
	}
	std::ifstream in(store_path(key), std::ios::binary);
	uint64_t count = 0;
	if (!in || !in.read(reinterpret_cast<char *>(&count), sizeof count) || count == 0 || count > (uint64_t{ 1 } << 28)) {
		return false;
	}
	block.resize(static_cast<size_t>(count));
	return static_cast<bool>(in.read(reinterpret_cast<char *>(block.data()), static_cast<std::streamsize>(count * sizeof(float))));
}

void optics_store_write(uint64_t key, const std::vector<float> &block) {
	const std::lock_guard<std::mutex> hold(store_lock());
	if (store_directory().empty() || block.empty()) {
		return;
	}
	std::ofstream out(store_path(key), std::ios::binary | std::ios::trunc);
	const uint64_t count = block.size();
	out.write(reinterpret_cast<const char *>(&count), sizeof count);
	out.write(reinterpret_cast<const char *>(block.data()), static_cast<std::streamsize>(count * sizeof(float)));
}

namespace {

// A glassware vessel's table of maps (lab/optics_trace.hpp: knots_block), made once for its inputs and kept for
// every scene; and, made ahead, read from the maps' store (GAME_SPEC §12.3, Stage 24: made before the game runs)
std::vector<float> shaped_block(const trace::MapSpec &spec, const glass::Vessel &vessel, double height, uint64_t key) {
	static std::mutex lock;
	static std::map<uint64_t, std::vector<float>> made;
	{
		const std::lock_guard<std::mutex> hold(lock);
		const auto found = made.find(key);
		if (found != made.end()) {
			return found->second;
		}
	}
	std::vector<float> block;
	if (!optics_store_read(key, block)) {
		block = trace::knots_block(trace::build_knots(spec, vessel, height));
		optics_store_write(key, block);
	}
	const std::lock_guard<std::mutex> hold(lock);
	return made.emplace(key, std::move(block)).first->second;
}

// (in `t`, the scene's table being made: each body's MAP and the tag its pixels' histories follow)
void place_maps(OpticsScene &scene, std::vector<float> &t, const std::vector<float> &rows, int count) {
	const double wall_px = t[2 * 4], gap_px = t[3 * 4];
	const auto put = [&](int vec, int part, double v) { t[static_cast<size_t>(vec) * 4 + static_cast<size_t>(part)] = static_cast<float>(v); };
	// Each refracting body's map: traced again only when its shape, its glass or its indices change; where
	// it stands and what fills it change nothing
	scene.map_keys.assign(static_cast<size_t>(MAX_BODIES), 0);
	std::vector<int> used(static_cast<size_t>(MAX_BODIES), -1); // each body's region of the maps
	std::vector<double> spans(static_cast<size_t>(MAX_BODIES), 0.0);
	std::vector<bool> shaped_of(static_cast<size_t>(MAX_BODIES), false);
	int64_t changed_from = INT64_MAX; // vec4s: from where the maps change
	for (int b = 0; b < MAX_BODIES; ++b) {
		const int at = HEADER_VECS + b * BODY_VECS;
		const float *rect = &t[static_cast<size_t>(at + RECT) * 4], *form = &t[static_cast<size_t>(at + FORM) * 4], *refract = &t[static_cast<size_t>(at + REFRACT) * 4];
		const bool refracts = b < count && refract[3] > 0.5f && (form[0] < 0.5f || form[0] > 1.5f);
		uint64_t key = 0;
		trace::MapSpec spec;
		const glass::Vessel *shaped = nullptr;
		if (refracts) {
			const bool prism = form[0] > 1.5f;
			const glass::Vessel *made = prism ? nullptr : scene.vessels[static_cast<size_t>(b)];
			spec.cm_per_pixel = form[1];
			if (made != nullptr) {
				// glassware stands with its axis on a whole pixel: its span from its width alone, as exact as the
				// maps made ahead for it (the float rectangle's own sum is off a whole pixel by its rounding)
				spec.left = -static_cast<double>(rect[2]) * 0.5 - 1.0;
				spec.right = static_cast<double>(rect[2]) * 0.5 + 1.0;
				spec.grid = 0.0;
			} else {
				const double axis = rect[0] + rect[2] * 0.5 + (prism ? t[static_cast<size_t>(at + GLOW) * 4 + 2] : 0.0f);
				spec.left = rect[0] - 1.0 - axis;
				spec.right = rect[0] + rect[2] + 1.0 - axis;
				spec.grid = std::floor(axis) - axis; // the screen's pixels, from its axis
			}
			// the wall, from its own plane: a gap further for each layer nearer the eye (in its own centimetres)
			spec.wall_cm = (wall_px + static_cast<double>(refract[1]) * gap_px) * spec.cm_per_pixel;
			const int set = static_cast<int>(t[static_cast<size_t>(at + SET) * 4]);
			spec.glass_index = &rows[static_cast<size_t>((set * ROWS_PER_SET + WALL_INDEX_ROW) * ROW)];
			if (prism) {
				const float *more = &t[static_cast<size_t>(at + MORE) * 4], *glow = &t[static_cast<size_t>(at + GLOW) * 4];
				const double corners[3][2] = { { more[0], more[1] }, { more[2], more[3] }, { glow[0], glow[1] } };
				spec.shape = trace::prism(corners);
				spec.media = 1;
				key = fnv(more, 4 * sizeof(float), fnv(glow, 3 * sizeof(float)));
			} else if (made != nullptr) {
				// glassware (Stage 24): its whole shape, a map for each band of its rows; its floor is in its shape
				shaped = made;
				spec.shape = trace::revolved_of(*made);
				spec.media = 2;
				spec.liquid_index = &rows[static_cast<size_t>((set * ROWS_PER_SET + LIQUID_INDEX_ROW) * ROW)];
				const double sized[2] = { made->nominal_ml, static_cast<double>(rect[3]) };
				key = fnv(made->kind.data(), made->kind.size(), fnv(sized, sizeof sized, fnv(spec.liquid_index, ROW * sizeof(float)))) ^ 0x9e3779b97f4a7c15ull;
			} else {
				spec.shape = trace::cylinder(rect[2] * 0.5 * form[1], refract[0]);
				spec.media = refract[2] > 0.0f ? 3 : 2; // with a floor, a third core: glass all through
				spec.liquid_index = &rows[static_cast<size_t>((set * ROWS_PER_SET + LIQUID_INDEX_ROW) * ROW)];
				key = fnv(refract, 3 * sizeof(float), fnv(spec.liquid_index, ROW * sizeof(float)));
			}
			const double span[6] = { spec.cm_per_pixel, spec.left, spec.right, spec.wall_cm, static_cast<double>(rect[2]), spec.grid };
			key = fnv(span, sizeof span, fnv(spec.glass_index, ROW * sizeof(float), key)) | 1u;
		}
		scene.map_keys[static_cast<size_t>(b)] = key;
		if (!refracts) {
			continue;
		}
		// where its map lies in the maps: where it was made before (kept while it lies there), or after them
		size_t region = 0;
		while (region < scene.map_regions.size() && scene.map_regions[region].key != key) {
			++region;
		}
		if (region == scene.map_regions.size()) {
			const std::vector<float> block = shaped ? shaped_block(spec, *shaped, rect[3], key) : trace::map_block(trace::build_map(spec));
			const int64_t end = static_cast<int64_t>(scene.maps.size() / 4);
			scene.maps.insert(scene.maps.end(), block.begin(), block.end());
			scene.map_regions.push_back(OpticsScene::MapRegion{ key, end, static_cast<int64_t>(block.size() / 4) });
			changed_from = std::min(changed_from, end);
		}
		used[static_cast<size_t>(b)] = static_cast<int>(region);
		spans[static_cast<size_t>(b)] = std::floor(spec.left - spec.grid) + spec.grid;
		shaped_of[static_cast<size_t>(b)] = shaped != nullptr;
	}
	// those no body looks through are let go once they outweigh those that are (and a few MB): the maps made
	// again from what is kept, from the start; till then a vessel set down and lifted again finds its maps there
	int64_t live = 0;
	std::vector<bool> kept(scene.map_regions.size(), false);
	for (const int region : used) {
		if (region >= 0 && !kept[static_cast<size_t>(region)]) {
			kept[static_cast<size_t>(region)] = true;
			live += scene.map_regions[static_cast<size_t>(region)].size;
		}
	}
	const int64_t idle = static_cast<int64_t>(scene.maps.size() / 4) - live;
	if (idle > live && idle > MAPS_IDLE_MOST) {
		std::vector<float> maps;
		std::vector<OpticsScene::MapRegion> regions;
		std::vector<int> moved(scene.map_regions.size(), -1);
		for (size_t r = 0; r < scene.map_regions.size(); ++r) {
			if (kept[r]) {
				const OpticsScene::MapRegion &old = scene.map_regions[r];
				moved[r] = static_cast<int>(regions.size());
				regions.push_back(OpticsScene::MapRegion{ old.key, static_cast<int64_t>(maps.size() / 4), old.size });
				maps.insert(maps.end(), scene.maps.begin() + old.at * 4, scene.maps.begin() + (old.at + old.size) * 4);
			}
		}
		for (int &region : used) {
			region = region >= 0 ? moved[static_cast<size_t>(region)] : -1;
		}
		scene.maps = std::move(maps);
		scene.map_regions = std::move(regions);
		changed_from = 0;
	}
	for (int b = 0; b < MAX_BODIES; ++b) {
		const int region = used[static_cast<size_t>(b)];
		if (region < 0) {
			continue;
		}
		const int at = HEADER_VECS + b * BODY_VECS;
		const int64_t map_at = scene.map_regions[static_cast<size_t>(region)].at;
		const float *block = &scene.maps[static_cast<size_t>(map_at) * 4];
		const bool shaped = shaped_of[static_cast<size_t>(b)];
		put(at + MAP, 0, static_cast<double>(map_at));
		put(at + MAP, 1, shaped ? block[static_cast<size_t>(block[5]) * 4] : block[0]); // the whole pixels it spans (a table: its first knot's map's)
		put(at + MAP, 2, spans[static_cast<size_t>(b)]);
		put(at + MAP, 3, shaped ? -block[0] : block[3]); // its pieces in wavelength; or, a table of its rows' bands, minus their count
		// a pixel's history follows the map it looks through
		const uint64_t key = scene.map_keys[static_cast<size_t>(b)];
		const float tagged = t[static_cast<size_t>(at + IDS) * 4 + 1];
		put(at + IDS, 1, tag(fnv(&key, sizeof key, fnv(&tagged, sizeof tagged))));
	}
	if (changed_from != INT64_MAX) {
		scene.maps_changed_from = changed_from;
		++scene.maps_revision;
	}
}

} // namespace

void optics_compose(OpticsScene &scene, const OpticsRoom &room, const std::vector<OpticsBody> &bodies) {
	std::vector<float> t(static_cast<size_t>(SIZE_VECS) * 4, 0.0f), rows, lines(static_cast<size_t>(MAX_LINES) * 4, 0.0f);
	const auto put = [&](int vec, int part, double v) { t[static_cast<size_t>(vec) * 4 + static_cast<size_t>(part)] = static_cast<float>(v); };
	put(0, 1, room.bench);
	put(0, 2, room.elsewhere);
	put(0, 3, LIGHTS);
	put(1, 0, room.x);
	put(1, 1, room.y);
	put(1, 2, room.w);
	put(1, 3, room.h);
	put(2, 0, room.wall_px);
	put(3, 0, room.gap_px);
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
			const std::string made = std::string(body.surface ? body.surface : "") + "|" + (body.light ? body.light : "") + "|" + (body.light_to ? body.light_to : "") +
									 "|" + std::to_string(body.toward) + "|" + std::to_string(body.strength);
			auto found = shared.find(made);
			if (found == shared.end()) {
				const int set = static_cast<int>(rows.size()) / (ROWS_PER_SET * ROW);
				rows.resize(static_cast<size_t>((set + 1) * ROWS_PER_SET * ROW), 0.0f);
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
				for (int i = 0; i < ROW && (a != nullptr || b != nullptr); ++i) {
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
			}
			put(at + RECT, 0, x);
			put(at + RECT, 1, y);
			put(at + RECT, 2, w);
			put(at + RECT, 3, h);
			put(at + FORM, 0, static_cast<int>(body.shape));
			put(at + FORM, 1, 1.0);
			put(at + FORM, 2, body.size);
			put(at + REFRACT, 1, layer_of(body));
			put(at + SET, 0, found->second);
			put(at + IDS, 0, tag(body.id != 0 ? body.id : fnv(made.data(), made.size())));
			uint64_t h_sig = fnv(&rows[static_cast<size_t>(found->second * ROWS_PER_SET * ROW)], ROWS_PER_SET * ROW * sizeof(float));
			h_sig = fnv(&t[static_cast<size_t>(at) * 4], 3 * 4 * sizeof(float), h_sig);
			h_sig = fnv(&wall_key, sizeof wall_key, h_sig);
			put(at + IDS, 1, tag(h_sig));
			continue;
		}
		const ItemView &v = *body.item;
		const int at = HEADER_VECS + count * BODY_VECS;
		// its own set of spectra
		const int set = static_cast<int>(rows.size()) / (ROWS_PER_SET * ROW);
		rows.resize(static_cast<size_t>((set + 1) * ROWS_PER_SET * ROW), 0.0f);
		put(at + SET, 0, set);
		float *liquid = &rows[static_cast<size_t>((set * ROWS_PER_SET + LIQUID_ROW) * ROW)];
		float *solid = &rows[static_cast<size_t>((set * ROWS_PER_SET + SOLID_ROW) * ROW)];
		float *wall = &rows[static_cast<size_t>((set * ROWS_PER_SET + WALL_ROW) * ROW)];
		float *liquid_index = &rows[static_cast<size_t>((set * ROWS_PER_SET + LIQUID_INDEX_ROW) * ROW)];
		float *wall_index = &rows[static_cast<size_t>((set * ROWS_PER_SET + WALL_INDEX_ROW) * ROW)];
		float *glows = &rows[static_cast<size_t>((set * ROWS_PER_SET + GLOW_ROW) * ROW)];
		for (int i = 0; i < ROW; ++i) {
			liquid_index[i] = wall_index[i] = 1.0f; // air
		}
		++count;
		const bool ball = v.kind == "balloon";
		const double bottom = static_cast<double>(body.y) + body.h;
		// a vessel made as glassware stands with its axis on a whole pixel (GAME_SPEC §12.3, Stage 24): its maps
		// are then the same wherever it stands, and are made before the game runs
		put(at + RECT, 0, v.glassware.empty() ? body.x : std::round(body.x + body.w / 2.0f) - body.w / 2.0f);
		put(at + RECT, 1, body.y);
		put(at + RECT, 2, body.w);
		put(at + RECT, 3, body.h);
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
			put(at + REFRACT, 3, room.refraction ? 1.0 : 0.0);
			put(at + IDS, 0, tag(fnv(v.id.data(), v.id.size())));
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
		if (v.liquid_ml > 0.0 || !v.glassware.empty()) {
			// a liquid's index is water's: the solutes' share is not modelled (GAME_SPEC §20); a glassware vessel's
			// liquid core is water's even while empty, so its maps are the same as it fills (Stage 24)
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
			// glassware in its standard's shape (GAME_SPEC §12.3, Stage 24): its rectangle is its widest by its
			// height; or, made to no standard, a cylinder of its volume and of the proportions it is drawn with
			const glass::Vessel *made = v.glassware.empty() ? nullptr : glass::find(v.glassware, v.nominal_ml);
			scene.vessels[static_cast<size_t>((at - HEADER_VECS) / BODY_VECS)] = made;
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
				put(at + FORM, 2, bottom - (made ? made->level(v.condensed_ml) : floor_cm + v.condensed_ml / floor_cm2) / cm_per_px);
			}
			if (solid_ml > 0.0 && v.stirred && v.liquid_ml > 0.0) {
				// stirred up: every particle shades its own cross-section, twice over
				put(at + MORE, 0, EXTINCTION_EFFICIENCY * cross_section_cm2 / v.liquid_ml);
			} else if (solid_ml > 0.0) {
				// at rest: a heap at its angle of repose, a cone until it reaches the wall
				const double heap_ml = solid_ml / PACKING, r_wall = made ? made->radius_inside(made->floor + 0.25 * made->height) : d_cm / 2.0;
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
				put(at + GLOW, c, rgb[c]); // its colour: a pixel's history follows it
			}
			glow_row(v.celsius + 273.15, reflectance, glows);
		}
		// which body this is, and a signature of what it holds and sees: a pixel's history follows them (GAME_SPEC §12.2)
		put(at + IDS, 0, tag(fnv(v.id.data(), v.id.size())));
		uint64_t h = fnv(liquid, ROWS_PER_SET * ROW * sizeof(float));
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
		scene.wall[2] = tag(wall_key); // the pixels that show the wall alone follow it
	}
	put(3, 1, nearest);
	place_maps(scene, t, rows, count);
	// where the chain may be needed: with the table, so the scene's revision covers it
	scene.chain.clear();
	scene.chain_bodies.clear();
	for (int b = 0; b < count; ++b) {
		const float *rect = &t[static_cast<size_t>(HEADER_VECS + b * BODY_VECS + RECT) * 4], *refract = &t[static_cast<size_t>(HEADER_VECS + b * BODY_VECS + REFRACT) * 4];
		const float *form = &t[static_cast<size_t>(HEADER_VECS + b * BODY_VECS + FORM) * 4];
		if (!(refract[3] > 0.5f) || !(form[0] < 0.5f || form[0] > 1.5f)) {
			continue;
		}
		// a map of several pieces (a prism) is read by the general reading; a table of rows' bands is of one piece
		// if each band's map is (Stage 24)
		const float *map = &t[static_cast<size_t>(HEADER_VECS + b * BODY_VECS + MAP) * 4];
		bool several = map[3] >= 0.0f ? map[3] != 1.0f : false;
		for (int k = 0; map[3] < 0.0f && k < static_cast<int>(-map[3]); ++k) {
			several = several || scene.maps[(static_cast<size_t>(map[0]) + 1 + static_cast<size_t>(k)) * 4 + 2] != 1.0f;
		}
		bool wanted = refract[1] > 0.5f || several;
		for (int o = 0; o < count && !wanted; ++o) {
			const float *other = &t[static_cast<size_t>(HEADER_VECS + o * BODY_VECS + RECT) * 4];
			// across another that bends, on its own layer (one of a nearer layer covers the pixels they share)
			const float *others = &t[static_cast<size_t>(HEADER_VECS + o * BODY_VECS + REFRACT) * 4];
			wanted = o != b && others[3] > 0.5f && others[1] == refract[1] && other[0] < rect[0] + rect[2] + 2.0f && other[0] + other[2] + 2.0f > rect[0] &&
					other[1] < rect[1] + rect[3] + 2.0f && other[1] + other[3] + 2.0f > rect[1];
		}
		if (wanted) {
			scene.chain.insert(scene.chain.end(), { std::floor(rect[0] - 2.0f), std::floor(rect[1] - 2.0f), std::ceil(rect[0] + rect[2] + 2.0f), std::ceil(rect[1] + rect[3] + 2.0f) });
			scene.chain_bodies.push_back(b);
		}
	}
	// What stands behind each body, in its rows, on a layer further from the eye: whether anything does, and
	// a signature of it (where it stands and what it is), which a pixel's history follows (GAME_SPEC §12.3)
	for (int b = 0; b < count; ++b) {
		const float *rect = &t[static_cast<size_t>(HEADER_VECS + b * BODY_VECS + RECT) * 4];
		const float layer = t[static_cast<size_t>(HEADER_VECS + b * BODY_VECS + REFRACT) * 4 + 1];
		bool lower = false;
		uint64_t h = 1469598103934665603ull;
		for (int o = 0; o < count; ++o) {
			const float *other = &t[static_cast<size_t>(HEADER_VECS + o * BODY_VECS + RECT) * 4];
			if (t[static_cast<size_t>(HEADER_VECS + o * BODY_VECS + REFRACT) * 4 + 1] < layer && other[1] < rect[1] + rect[3] + 1.0f && other[1] + other[3] + 1.0f > rect[1]) {
				lower = true;
				h = fnv(other, 4 * sizeof(float), h);
				h = fnv(&t[static_cast<size_t>(HEADER_VECS + o * BODY_VECS + FORM) * 4], 4 * sizeof(float), h);
				h = fnv(&t[static_cast<size_t>(HEADER_VECS + o * BODY_VECS + IDS) * 4], 2 * sizeof(float), h);
			}
		}
		put(HEADER_VECS + b * BODY_VECS + SET, 1, lower ? 1.0 : 0.0);
		put(HEADER_VECS + b * BODY_VECS + SET, 2, lower ? tag(h) : 0.0);
	}
	// the screen's squares: each the bodies that reach into it, in painting order, the nearest kept
	for (int b = 0; b < count; ++b) {
		const float *rect = &t[static_cast<size_t>(HEADER_VECS + b * BODY_VECS + RECT) * 4];
		const auto square = [](double v, int most) { return std::min(std::max(static_cast<int>(std::floor(v / GRID_SQUARE)), 0), most - 1); };
		const int c0 = square(rect[0] - 1.5, GRID_COLS), c1 = square(static_cast<double>(rect[0]) + rect[2] + 1.5, GRID_COLS);
		const int r0 = square(rect[1] - 1.5, GRID_ROWS), r1 = square(static_cast<double>(rect[1]) + rect[3] + 1.5, GRID_ROWS);
		for (int r = r0; r <= r1; ++r) {
			for (int c = c0; c <= c1; ++c) {
				float *cell = &t[static_cast<size_t>(GRID_BASE + 2 * (r * GRID_COLS + c)) * 4];
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
	if (t != scene.table || rows != scene.rows || lines != scene.lines) {
		scene.table = std::move(t);
		scene.rows = std::move(rows);
		scene.lines = std::move(lines);
		++scene.revision;
	}
	optics_stacks(scene); // the rays of what is held, followed through what stands behind it
}

void optics_maps(OpticsScene &scene) {
	if (scene.table.size() != static_cast<size_t>(SIZE_VECS) * 4 || scene.rows.size() % static_cast<size_t>(ROWS_PER_SET * ROW) != 0) {
		return;
	}
	place_maps(scene, scene.table, scene.rows, static_cast<int>(scene.table[0]));
	++scene.revision;
	scene.stack_keys.clear(); // followed for other maps
	optics_stacks(scene);
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
