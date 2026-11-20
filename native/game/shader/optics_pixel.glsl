// One pixel of the optical pass (GAME_SPEC §12, §12.2): the room's light carried
// through every body in front of the backdrop at a few moving wavelengths a
// frame, each the middle of a band of equal energy whose exact colour it
// scales; each pixel's history kept while its content is the same or a fixed
// estimate says its colour barely moved, and points spent only where a pixel
// still needs them.
//
// This file is the renderer. The Godot host compiles it inside a compute shader
// (GLSL 450: native/hosts/godot/optics_compute.cpp); the lab layer compiles the
// same text as C++ in float64 (native/game/src/optics_reference.cpp), and that
// build is the reference. So it keeps to what both languages read alike: no
// swizzles, no out or inout parameters, no constructors but vec2(a, b), vec3(a),
// vec3(a, b, c), vec4(a) and vec4(a, b, c, d); 32-bit unsigned arithmetic as uint.
//
// The includer provides (lab/optics_scene.hpp lays them out):
//   vec4 scene_vec(int i)    the header and the bodies
//   float row_value(int i)   each body's spectra at 1 nm from 380 nm: its liquid's optical
//                            depth per centimetre, its solids' reflectance, its wall's depth
//   vec4 line_vec(int i)     narrow lines: centre (nm), full width at half maximum, depth per cm at the peak
//   vec4 light_vec(int i)    the room light's energy table: nm - 380, and its colour up to there, X Y Z;
//                            and after its LIGHT_ENTRIES, the share of its energy up to each whole nm (x)
//   vec4 map_vec(int i)      each refracting body's map (lab/optics_trace.hpp): where its rays land
//   vec4 wall_vec(int i)     the wall as a table (lab/optics_scene.hpp): what is drawn behind the bodies
//   vec4 stack_vec(int i)    the stacks (lab/optics_scene.hpp): the rays followed through several bodies

const int HEADER_VECS = 4;
const int BODY_VECS = 7;
const int ROW = 401;
const int ROWS_PER_BODY = 6;
const int LIGHT_ENTRIES = 4097;
const int MAX_LAYERS = 4; // bodies one pixel looks through
const float WALL_MOST = 4.0; // a wall seen edge on is no thicker than this, in reference thicknesses

// the method (GAME_SPEC §12.2, item 4 and item 5)
const int PROXY_POINTS = 8; // the fixed estimate: 8 bands, each read at its middle
const int LEAST_BATCH = 16; // a frame's batch is never smaller: fewer bands settle on a wrong colour
const float FRESH_POINTS = 64.0; // a pixel whose history has just started
const float MOST_POINTS = 64.0; // in one frame
const float SETTLED_POINTS = 128.0; // a pixel may stop only after these, and after SETTLED_FRAMES
const float SETTLED_FRAMES = 4.0;
const float TARGET = 2.0; // the expected error a pixel's average is brought to, on the eye metric
const float DRIFT_MOST = 4.0; // how far the fixed estimate may move, summed, before the history starts again

struct Layer {
	int body;
	float depth_cm; // the liquid crossed, in centimetres
	float haze; // how much of the room's light its suspended solids turn back
	float solid; // how much of the pixel a heap of solid fills
	float wall; // the wall crossed, in reference thicknesses
	float wet; // how much of the pixel is in its liquid
	float cover; // how much of the pixel it covers
};

// the layers are four named fields, not an array: written at a runtime index, an array lives in a GPU's
// slow scratch memory, and every point of every pixel reads it
struct Path {
	Layer l0;
	Layer l1;
	Layer l2;
	Layer l3;
	int layers;
	float back; // the backdrop's radiance, a share of the room's white
	float cover; // how much of the pixel a body covers
	vec3 glow; // what hot solids emit, linear sRGB: for the key alone (the pass reads each body's glow at 1 nm)
	vec4 key; // everything the pixel's colour depends on that can change
	// where it looks from, how wide a sample it is, and the vessel that bends its view (GAME_SPEC §12.3), if
	// any: the nearest the eye of those it looks through
	float x;
	float y;
	float size;
	int refractor; // that body, or -1
	float raxis; // its axis on the screen (pixels)
	float rlayer; // its layer
	int rmap; // its map: where it starts among the maps, the whole pixels it spans, its samples, pieces in wavelength and cores
	int rcoarse;
	int rsamples;
	int rpieces;
	int rmedia;
	float rleft; // the map's first sample, pixels from the axis
	float rwet; // how much of the pixel's height is under its liquid's surface
	float rin; // how much of the pixel's height is between its top and its foot
	float rfloor; // how much of the pixel's height is in its floor (glass all through: its map's third core)
	// its rays are followed on (GAME_SPEC §12.3, Stage 21): it looks through more than one body that bends,
	// or bodies of a layer behind its vessel's stand in its rows
	bool chain;
};

// a pixel's state between frames
struct PixelState {
	vec4 hist; // what the moving points add to the fixed estimate (linear sRGB, averaged), and the points behind it
	vec4 key; // the content key it belongs to
	vec4 prox; // the fixed estimate (linear sRGB), and how far it has moved since the history began
	vec4 aux; // the noise of one point (eye metric, squared), frames in the history, points asked for, points spent
};

// ---- where the pixel looks

// What body i is at the place p of the screen, for a sample `across` wide and `up` high (pixels): how much
// of the sample it covers (its outlines blend by that), and what the sample looks through there.
Layer layer_at(int i, vec2 p, float across, float up) {
	Layer l;
	l.body = i;
	l.depth_cm = 0.0;
	l.haze = 0.0;
	l.solid = 0.0;
	l.wall = 0.0;
	l.wet = 0.0;
	l.cover = 0.0;
	int at = HEADER_VECS + i * BODY_VECS;
	vec4 rect = scene_vec(at);
	vec4 form = scene_vec(at + 1);
	vec4 more = scene_vec(at + 2);
	float radius = rect.z * 0.5;
	float from_axis = p.x - (rect.x + radius);
	float dx = from_axis / radius;
	float ground = rect.y + rect.w; // where its solids rest, under this place
	float side = 1.0; // how much of the sample is within it, across
	float under = 0.0; // how much of the sample is under where its contents rest
	float within = 0.0; // samples inside its outline
	float chord = 0.0; // the path through it, in diameters
	float wall = 0.0; // the wall crossed, in reference thicknesses
	if (form.x > 1.5) {
		// a prism standing upright: seen from the side its silhouette is its rectangle
		within = min(min(p.x - rect.x, rect.x + rect.z - p.x) / across, min(p.y - rect.y, ground - p.y) / up);
	} else if (form.x < 0.5) {
		// a cylinder seen from the side
		within = min(radius * (1.0 - abs(dx)) / across, min(p.y - rect.y, ground - p.y) / up);
		chord = sqrt(max(1.0 - dx * dx, 0.0));
		// what it holds rests on its floor, which is glass all through
		side = clamp(radius * (1.0 - abs(dx)) / across + 0.5, 0.0, 1.0);
		ground = ground - scene_vec(at + 5).z / form.y;
		under = clamp((p.y - ground) / up + 0.5, 0.0, 1.0);
	} else {
		// a ball: its skin is crossed twice, and aslant near the rim
		float half_height = rect.w * 0.5;
		float dy = (p.y - (rect.y + half_height)) / half_height;
		float d = sqrt(dx * dx + dy * dy);
		within = min(radius, half_height) * (1.0 - d) / max(across, up);
		chord = sqrt(max(1.0 - d * d, 0.0));
		wall = min(2.0 * more.y / max(chord, 0.001), WALL_MOST);
		ground = rect.y + half_height + half_height * sqrt(max(1.0 - dx * dx, 0.0));
	}
	float cover = clamp(within + 0.5, 0.0, 1.0);
	if (cover <= 0.0) {
		return l;
	}
	float cm = 2.0 * radius * chord * form.y;
	// a sample half under the surface is half as deep in it: T^coverage
	float wet = form.x < 0.5 ? max(clamp((p.y - form.z) / up + 0.5, 0.0, 1.0) - under, 0.0) * side : clamp((p.y - form.z) / up + 0.5, 0.0, 1.0) * cover;
	float heap = form.x > 1.5 ? 0.0 : form.w * max(1.0 - abs(from_axis) / max(more.z, 0.001), 0.0);
	l.depth_cm = cm * wet;
	l.haze = form.x > 1.5 ? 0.0 : 1.0 - exp(-more.x * cm * wet);
	l.solid = form.w <= 0.0 ? 0.0 : (form.x < 0.5 ? max(clamp((p.y - (ground - heap)) / up + 0.5, 0.0, 1.0) - under, 0.0) * side : clamp((p.y - (ground - heap)) / up + 0.5, 0.0, 1.0) * cover);
	l.wall = wall * cover;
	l.wet = wet;
	l.cover = cover;
	return l;
}

// what the pixel at p looks through; `size` is how wide a sample it is, in pixels: 1 for a pixel (its
// outlines blend by how much of it they cover), less for a sample of the truth (GAME_SPEC §12.3)
Path path_of(vec2 p, float size) {
	Path q;
	q.layers = 0;
	q.cover = 0.0;
	q.glow = vec3(0.0);
	q.key = vec4(0.0);
	q.x = p.x;
	q.y = p.y;
	q.size = size;
	q.refractor = -1;
	q.raxis = 0.0;
	q.rlayer = 0.0;
	q.rmap = 0;
	q.rcoarse = 0;
	q.rsamples = 0;
	q.rpieces = 0;
	q.rmedia = 0;
	q.rleft = 0.0;
	q.rwet = 0.0;
	q.rin = 0.0;
	q.rfloor = 0.0;
	q.chain = false;
	vec4 head = scene_vec(0);
	vec4 bench = scene_vec(1);
	bool on_bench = p.x >= bench.x && p.x < bench.x + bench.z && p.y >= bench.y && p.y < bench.y + bench.w;
	q.back = on_bench ? head.y : head.z;
	bool layered = scene_vec(3).y > 0.5; // something is on a layer nearer than the bench's
	float lowest = 99.0; // the furthest layer any body in the pixel's rows is on
	int bending = 0;
	vec4 behind = vec4(0.0);
	int count = int(head.x);
	for (int i = 0; i < 16; i++) {
		if (i >= count) {
			break;
		}
		int at = HEADER_VECS + i * BODY_VECS;
		vec4 rect = scene_vec(at);
		if (p.y < rect.y - 1.0 || p.y > rect.y + rect.w + 1.0) {
			continue;
		}
		if (layered) {
			// in the pixel's rows: a nearer layer's vessel may bend the pixel's rays into it, wherever across
			// it stands, so where it stands and what it holds are the pixel's content too
			vec4 beside = scene_vec(at + 4);
			float w = 1.0 + 0.6180339887 * float(i);
			lowest = min(lowest, scene_vec(at + 5).y);
			behind = behind + vec4(0.23 * (rect.x - p.x) * w + 0.07 * rect.z, 0.11 * (rect.y - p.y) * w + 0.05 * (scene_vec(at + 1).z - p.y), 0.5 * beside.x, beside.y * w);
		}
		if (p.x < rect.x - 1.0 || p.x > rect.x + rect.z + 1.0) {
			continue;
		}
		Layer l = layer_at(i, p, size, size);
		if (l.cover <= 0.0 || q.layers >= MAX_LAYERS) {
			continue;
		}
		vec4 form = scene_vec(at + 1);
		float radius = rect.z * 0.5;
		vec4 bend = scene_vec(at + 5);
		if (bend.w > 0.5) {
			// a vessel bends the view: the nearest the eye of those the pixel looks through
			bending = bending + 1;
			q.refractor = i;
			q.raxis = rect.x + radius;
			q.rlayer = bend.y;
			if (form.x > 1.5) {
				q.raxis = rect.x + rect.z * 0.5 + scene_vec(at + 3).z; // a prism's centre, which its corners are given from
			}
			vec4 map = scene_vec(at + 6);
			q.rmap = int(map.x);
			q.rcoarse = int(map.y);
			q.rleft = map.z;
			q.rpieces = int(map.w);
			vec4 map_head = map_vec(q.rmap);
			q.rmedia = int(map_head.y);
			q.rsamples = int(map_head.z);
			// the rows of the pixel under its liquid's surface, and those between its top and its floor
			q.rwet = clamp((p.y - form.z) / size + 0.5, 0.0, 1.0);
			q.rin = clamp(min(p.y - rect.y, rect.y + rect.w - p.y) / size + 0.5, 0.0, 1.0);
			// and those in its floor, from the floor's top to the vessel's foot
			q.rfloor = q.rmedia > 2 ? clamp((min(p.y + 0.5 * size, rect.y + rect.w) - max(p.y - 0.5 * size, rect.y + rect.w - bend.z / form.y)) / size, 0.0, 1.0) : 0.0;
		}
		if (q.layers == 0) {
			q.l0 = l;
		} else if (q.layers == 1) {
			q.l1 = l;
		} else if (q.layers == 2) {
			q.l2 = l;
		} else {
			q.l3 = l;
		}
		q.layers = q.layers + 1;
		vec4 glow = form.x > 1.5 ? vec4(0.0) : scene_vec(at + 3);
		q.glow = mix(q.glow, vec3(glow.x, glow.y, glow.z), l.solid);
		q.cover = max(q.cover, l.cover);
		vec4 ids = scene_vec(at + 4);
		float w = 1.0 + 0.6180339887 * float(i);
		q.key = q.key + vec4((l.depth_cm + 3.0 * l.haze + 0.29 * l.cover) * w, (l.solid + 7.0 * l.wall) * w, ids.x, ids.y * w);
	}
	// what glows is a light in the pass (each body's glow at 1 nm); its colour is in the key so that a pixel shows it anew
	q.key.y = q.key.y + 13.0 * q.back + 0.001 * (q.glow.x + 2.0 * q.glow.y + 3.0 * q.glow.z);
	// where the view goes through a refracting body: where across it, and how much of the pixel is under its liquid and within it
	if (q.refractor >= 0) {
		q.key.x = q.key.x + 0.37 * (q.x - q.raxis) + 0.19 * q.rwet + 0.43 * q.rin + 0.29 * q.rfloor;
		q.key.y = q.key.y + 0.31 * q.raxis;
		q.chain = bending > 1 || lowest < q.rlayer;
		if (q.chain) {
			q.key = q.key + behind;
		}
	}
	return q;
}

// whether any body's rectangle is near the pixel: most of the screen is not, and needs nothing more
bool near_any(vec2 p) {
	int count = int(scene_vec(0).x);
	for (int i = 0; i < 16; i++) {
		if (i >= count) {
			break;
		}
		vec4 rect = scene_vec(HEADER_VECS + i * BODY_VECS);
		if (p.x >= rect.x - 1.0 && p.x <= rect.x + rect.z + 1.0 && p.y >= rect.y - 1.0 && p.y <= rect.y + rect.w + 1.0) {
			return true;
		}
	}
	return false;
}

// ---- what gets through at one wavelength

float row_at(int start, float nm) {
	float x = clamp(nm - 380.0, 0.0, 400.0);
	int i = min(int(floor(x)), 399);
	float f = x - float(i);
	return row_value(start + i) * (1.0 - f) + row_value(start + i + 1) * f;
}

float liquid_at(int body, float nm) {
	float depth = row_at(body * ROWS_PER_BODY * ROW, nm);
	vec4 ids = scene_vec(HEADER_VECS + body * BODY_VECS + 4);
	int first = int(ids.z);
	int count = int(ids.w);
	for (int j = 0; j < 256; j++) {
		if (j >= count) {
			break;
		}
		vec4 l = line_vec(first + j);
		float u = (nm - l.x) / l.y;
		depth = depth + l.z * exp(-2.7725887 * u * u);
	}
	return depth;
}

// what one body does to the light that reaches it, at nm, through depth_cm of its liquid
float through_layer(Layer l, float seen_before, float nm, float depth_cm) {
	int start = l.body * ROWS_PER_BODY * ROW;
	float depth = liquid_at(l.body, nm) * depth_cm;
	float colour = row_at(start + ROW, nm);
	// through the liquid (Beer-Lambert)
	float seen = seen_before * exp(-depth);
	// what is suspended in it turns the room's light back, from half way in on average
	seen = mix(seen, colour * exp(-depth * 0.5), l.haze);
	// what lies in it is lit and seen through it: there and back is one chord; and glows, through half of one
	seen = mix(seen, colour * exp(-depth) + row_at(start + 5 * ROW, nm) * exp(-depth * 0.5), l.solid);
	// and the wall
	return seen * exp(-row_at(start + 2 * ROW, nm) * l.wall);
}

// ---- the wall behind the bodies (GAME_SPEC §12.3): a table, read exactly

const int WALL_ROWS = 1080;
const int WALL_COLUMNS = 1920;
const int WALL_ROW_BASE = 1;
const int WALL_COLUMN_BASE = 1081;
const int WALL_BREAK_BASE = 3001;

// A place on a row of the wall: the cell it is in, how far it is from the cell's left end (the first cell
// has none: from breakpoint 0, behind it), the cell's radiance, the row's integral from breakpoint 0
// to that left end, in its two parts; and where the cell begins and ends, with the place `from` is counted
// from: a ray that moves within its cell reads nothing more.
struct WallAt {
	int cell;
	float from;
	float radiance;
	float coarse;
	float fine;
	float base;
	float left;
	float right;
};

WallAt wall_at(int profile, float x) {
	int m = int(wall_vec(0).x);
	// the cell: how many breakpoints are at or left of x; from the column's, a few steps
	int j = x < 0.0 ? 0 : int(wall_vec(WALL_COLUMN_BASE + min(int(x), WALL_COLUMNS - 1)).x);
	float right = x; // not known: a ray that moves looks again
	for (int k = 0; k < 64; k++) {
		if (j >= m) {
			right = 1.0e30;
			break;
		}
		float next = wall_vec(WALL_BREAK_BASE + j).x;
		if (next > x) {
			right = next;
			break;
		}
		j = j + 1;
	}
	vec4 c = wall_vec(WALL_BREAK_BASE + m + profile * (m + 1) + j);
	WallAt w;
	w.cell = j;
	w.base = wall_vec(WALL_BREAK_BASE + max(j - 1, 0)).x;
	w.from = x - w.base;
	w.radiance = c.x;
	w.coarse = c.y;
	w.fine = c.z;
	w.left = j > 0 ? w.base : -1.0e30;
	w.right = right;
	return w;
}

// the place x, from a place near it on the same row
WallAt wall_move(int profile, WallAt w, float x) {
	if (x >= w.left && x < w.right) {
		w.from = x - w.base;
		return w;
	}
	return wall_at(profile, x);
}

// the row's integral from place r to place w: within a cell exactly its radiance times the way
float wall_between(WallAt r, WallAt w) {
	if (w.cell == r.cell) {
		return w.radiance * (w.from - r.from);
	}
	return (w.coarse - r.coarse) + (w.fine - r.fine) + w.radiance * w.from - r.radiance * r.from;
}

// the mean radiance of the row from a to b
float wall_span(int profile, float a, float b) {
	float lo = min(a, b);
	float hi = max(a, b);
	WallAt w = wall_at(profile, lo);
	if (hi - lo < 1.0e-5) {
		return w.radiance;
	}
	return wall_between(w, wall_at(profile, hi)) / (hi - lo);
}

// The mean, over the places from x0 to x1 (at0 and at1 are those places), of the row's integral from place
// r. Cell by cell, in each of which the integral is a straight line: exact, however many edges lie between.
float wall_mean(int profile, WallAt r, WallAt at0, float x0, WallAt at1, float x1) {
	WallAt a = at0;
	WallAt b = at1;
	float lo = x0;
	float hi = x1;
	if (x1 < x0) {
		a = at1;
		b = at0;
		lo = x1;
		hi = x0;
	}
	float s_a = wall_between(r, a);
	if (a.cell == b.cell || hi - lo < 1.0e-5) {
		return s_a + 0.5 * a.radiance * (hi - lo);
	}
	int m = int(wall_vec(0).x);
	float sum = 0.0;
	float at = lo;
	float s_at = s_a;
	for (int k = 0; k < 4096; k++) {
		int cell = a.cell + k;
		if (cell > b.cell) {
			break;
		}
		float end = cell == b.cell ? hi : wall_vec(WALL_BREAK_BASE + cell).x; // a cell ends at its own breakpoint
		float s_end = s_at + wall_vec(WALL_BREAK_BASE + m + profile * (m + 1) + cell).x * (end - at);
		sum = sum + 0.5 * (s_at + s_end) * (end - at);
		at = end;
		s_at = s_end;
	}
	return sum / (hi - lo);
}

// ---- a body's refraction map (GAME_SPEC §12.3; lab/optics_trace.hpp lays it out)

const float MAP_FINE = 4.0; // samples a pixel: a pixel is read in four parts at most

// what a ray does in one body: where it lands on the body's own wall (x, pixels from its axis), what the body's
// surfaces let through (pass, Fresnel), the chord of its liquid it crosses (cm), and its route (the same for
// rays that go the same way)
struct Bend {
	float x;
	float pass;
	float chord;
	float route;
};

// where a wavelength is among a map's pieces: 0 at 380 nm, 1 at 780 nm, even in 1 / nm^2 (what glass and
// water disperse nearly straight in)
float map_place(float nm) {
	float r = 380.0 / clamp(nm, 380.0, 780.0);
	return clamp((1.0 - r * r) / 0.7626561473, 0.0, 1.0);
}

// A map, as a pixel's rays read it: where it starts among the maps, the whole pixels it spans, its samples,
// its pieces in wavelength, its cores, and its first sample's place (pixels from its body's axis).
struct MapRef {
	int at;
	int coarse;
	int samples;
	int pieces;
	int media;
	float left;
};

// the map of the vessel that bends the pixel's view
MapRef ref_of(Path q) {
	MapRef m;
	m.at = q.rmap;
	m.coarse = q.rcoarse;
	m.samples = q.rsamples;
	m.pieces = q.rpieces;
	m.media = q.rmedia;
	m.left = q.rleft;
	return m;
}

// A ray of a body at every wavelength of one piece of its map: at the piece's four nodes (its ends and its
// thirds) where it lands (pixels from the body's axis), what the surfaces pass, its chord squared, its route.
struct Edge {
	vec4 land;
	vec4 pass;
	vec4 chord;
	vec4 route;
};

// The ray at x (pixels from the body's axis), through its core as liquid (medium 0 of two) or as air, in
// one piece of the map: between two samples across, the smooth curve through them and their two neighbours
// (straight, if a neighbour goes another way; the nearer one's ray where the route changes between them); on
// a sample (a pixel's parts begin and end on samples, wherever a body stands on whole or
// half pixels), that sample's cell alone. Past the map's ends a ray misses the body and goes straight.
// A route is a small whole number, and 0 is the ray that misses the body (the map's first sample is past
// its outline): -1, past the map's ends, is that too.
Edge edge_load(MapRef m, int medium, float x, int piece) {
	Edge e;
	e.land = vec4(x, x, x, x);
	e.pass = vec4(1.0, 1.0, 1.0, 1.0);
	e.chord = vec4(0.0, 0.0, 0.0, 0.0);
	e.route = vec4(-1.0, -1.0, -1.0, -1.0);
	float s = (x - m.left) * MAP_FINE;
	if (m.samples < 2 || s < 0.0 || s > float(m.samples - 1)) {
		return e;
	}
	int i = min(int(floor(s)), m.samples - 2);
	float f = s - float(i);
	int base = m.at + 1 + m.coarse + ((medium * m.samples + i) * m.pieces + piece) * 4;
	vec4 r = map_vec(base + 3);
	if (f > 0.0) {
		int next = base + m.pieces * 4;
		vec4 r2 = map_vec(next + 3);
		// where the route changes between the samples (how far along: twice the route's fraction), each side is its own sample's ray
		if (floor(r.x) != floor(r2.x)) {
			f = f < 2.0 * fract(r.x) ? 0.0 : 1.0;
		} else if (floor(r.y) != floor(r2.y)) {
			f = f < 2.0 * fract(r.y) ? 0.0 : 1.0;
		} else if (floor(r.z) != floor(r2.z)) {
			f = f < 2.0 * fract(r.z) ? 0.0 : 1.0;
		} else if (floor(r.w) != floor(r2.w)) {
			f = f < 2.0 * fract(r.w) ? 0.0 : 1.0;
		}
		if (f >= 1.0) {
			base = next;
			r = r2;
		} else if (f > 0.0) {
			e.route = vec4(floor(r.x), floor(r.y), floor(r.z), floor(r.w));
			if (i > 0 && i + 2 < m.samples) {
				// between two samples whose neighbours go the same way: the smooth curve through the four (a
				// ray followed in from another body comes in anywhere, and a thin line's image is as bright as
				// the landing's slope is small: the slope must be the map's, not a chord's of it)
				int before = base - m.pieces * 4;
				int after = next + m.pieces * 4;
				vec4 r0 = map_vec(before + 3);
				vec4 r3 = map_vec(after + 3);
				if (floor(r0.x) == e.route.x && floor(r0.y) == e.route.y && floor(r0.z) == e.route.z && floor(r0.w) == e.route.w && floor(r3.x) == e.route.x && floor(r3.y) == e.route.y &&
						floor(r3.z) == e.route.z && floor(r3.w) == e.route.w) {
					float w0 = 0.5 * f * (-1.0 + f * (2.0 - f));
					float w1 = 0.5 * (2.0 + f * f * (-5.0 + 3.0 * f));
					float w2 = 0.5 * f * (1.0 + f * (4.0 - 3.0 * f));
					float w3 = 0.5 * f * f * (f - 1.0);
					e.land = map_vec(before) * w0 + map_vec(base) * w1 + map_vec(next) * w2 + map_vec(after) * w3;
					e.pass = map_vec(before + 1) * w0 + map_vec(base + 1) * w1 + map_vec(next + 1) * w2 + map_vec(after + 1) * w3;
					e.chord = map_vec(before + 2) * w0 + map_vec(base + 2) * w1 + map_vec(next + 2) * w2 + map_vec(after + 2) * w3;
					return e;
				}
			}
			e.land = mix(map_vec(base), map_vec(next), f);
			e.pass = mix(map_vec(base + 1), map_vec(next + 1), f);
			e.chord = mix(map_vec(base + 2), map_vec(next + 2), f);
			return e;
		}
	}
	e.land = map_vec(base);
	e.pass = map_vec(base + 1);
	e.chord = map_vec(base + 2);
	e.route = vec4(floor(r.x), floor(r.y), floor(r.z), floor(r.w));
	return e;
}

// the cubic through four values at the thirds of a piece, at g of the way through it: what each value weighs
vec4 thirds_weights(float g) {
	float s = 3.0 * g;
	return vec4(-(s - 1.0) * (s - 2.0) * (s - 3.0) / 6.0, s * (s - 2.0) * (s - 3.0) / 2.0, -s * (s - 1.0) * (s - 3.0) / 2.0, s * (s - 1.0) * (s - 2.0) / 6.0);
}

float thirds(vec4 v, float g) {
	return dot(thirds_weights(g), v);
}

// the weight of the node nearest to g of the way through a piece: all of it
vec4 nearest_weights(float g) {
	return g < 0.5 ? (g < 0.1666667 ? vec4(1.0, 0.0, 0.0, 0.0) : vec4(0.0, 1.0, 0.0, 0.0)) : (g < 0.8333333 ? vec4(0.0, 0.0, 1.0, 0.0) : vec4(0.0, 0.0, 0.0, 1.0));
}

// one of four values: the node nearest to g of the way through the piece
float nearest(vec4 v, float g) {
	return g < 0.5 ? (g < 0.1666667 ? v.x : v.y) : (g < 0.8333333 ? v.z : v.w);
}

// the ray at g of the way through its piece (where it lands: pixels from its body's axis): the cubic through
// its four nodes; or, if its route changes in the piece, its nearest node's ray
Bend edge_at(Edge e, float g) {
	Bend b;
	if (e.route.y == e.route.x && e.route.z == e.route.x && e.route.w == e.route.x) {
		b.x = thirds(e.land, g);
		b.pass = clamp(thirds(e.pass, g), 0.0, 1.0);
		b.chord = sqrt(max(thirds(e.chord, g), 0.0)); // the map holds its square
		b.route = e.route.x;
	} else {
		b.x = nearest(e.land, g);
		b.pass = nearest(e.pass, g);
		b.chord = sqrt(max(nearest(e.chord, g), 0.0));
		b.route = nearest(e.route, g);
	}
	return b;
}

// the ray at x and nm
Bend map_at(MapRef m, int medium, float x, float nm) {
	float place = map_place(nm) * float(m.pieces);
	int piece = min(int(floor(place)), m.pieces - 1);
	return edge_at(edge_load(m, medium, x, piece), place - float(piece));
}

// into how many equal parts the whole pixel of the body that x is in must be read
float map_level(MapRef m, float x) {
	int c = int(floor(x - m.left));
	if (c < 0 || c >= m.coarse) {
		return 1.0;
	}
	return map_vec(m.at + 1 + c).x;
}

// ---- what reaches the eye

// The chord that absorbs as the rays of a part do, together (GAME_SPEC §12.3): their chords run from c0 to
// c1, the square straight across the part (as a chord's is, exactly, where it rises as a root at a rim),
// and the liquid's optical depth is `per_cm` a centimetre. The mean of e^(-per_cm c) over the part, in closed
// form, as one chord's.
float chord_of_part(float c0, float c1, float per_cm) {
	float lo = min(c0, c1);
	float hi = max(c0, c1);
	if (per_cm * (hi - lo) < 1.0e-3) {
		return 0.5 * (lo + hi);
	}
	// (2 / (b k^2)) ((k lo + 1) e^(-k lo) - (k hi + 1) e^(-k hi)), b = hi^2 - lo^2; e^(-k lo) taken out
	float through_part = 2.0 / ((hi * hi - lo * lo) * per_cm * per_cm) * (per_cm * lo + 1.0 - (per_cm * hi + 1.0) * exp(-per_cm * (hi - lo)));
	return lo - log(max(through_part, 1.0e-30)) / per_cm;
}

// What the bodies a pixel looks through do to the wall's light at one wavelength (GAME_SPEC §12.2): read
// once for the wavelength, then applied to each part of the pixel by arithmetic alone. The layers behind the
// refracting body and those in front of it each take what they are given to a times it plus b; the
// refracting body's own liquid is crossed along each part's own chords, so its spectra are kept as read.
struct Shade {
	float a_behind;
	float b_behind;
	float a_front;
	float b_front;
	bool own; // the refracting body is among the layers
	float per_cm; // its liquid's optical depth a centimetre
	float colour; // what its solids and its haze turn back
	float glow; // what its solids glow with
	float glass; // what its wall lets through
	float haze;
	float solid;
	float room; // the room in front, which the surfaces turn back
};

Shade shade_layer(Shade s, Path q, Layer l, int k, float nm) {
	if (k >= q.layers) {
		return s;
	}
	int start = l.body * ROWS_PER_BODY * ROW;
	float per_cm = liquid_at(l.body, nm);
	float colour = row_at(start + ROW, nm);
	float glass = exp(-row_at(start + 2 * ROW, nm) * l.wall);
	float glow = row_at(start + 5 * ROW, nm); // what its solids glow with, a multiple of the room's light
	if (l.body == q.refractor && !s.own) {
		s.own = true;
		s.per_cm = per_cm;
		s.colour = colour;
		s.glow = glow;
		s.glass = glass;
		s.haze = l.haze;
		s.solid = l.solid;
		return s;
	}
	// through_layer, as a times what reaches it plus b
	float depth = per_cm * l.depth_cm;
	float through = exp(-depth);
	float a = through * (1.0 - l.haze) * (1.0 - l.solid) * glass;
	float b = (colour * (exp(-depth * 0.5) * l.haze * (1.0 - l.solid) + through * l.solid) + glow * exp(-depth * 0.5) * l.solid) * glass;
	if (s.own) {
		s.b_front = a * s.b_front + b;
		s.a_front = a * s.a_front;
	} else {
		s.b_behind = a * s.b_behind + b;
		s.a_behind = a * s.a_behind;
	}
	return s;
}

Shade shade_of(Path q, float nm) {
	Shade s;
	s.a_behind = 1.0;
	s.b_behind = 0.0;
	s.a_front = 1.0;
	s.b_front = 0.0;
	s.own = false;
	s.per_cm = 0.0;
	s.colour = 0.0;
	s.glow = 0.0;
	s.glass = 1.0;
	s.haze = 0.0;
	s.solid = 0.0;
	s.room = scene_vec(2).y;
	s = shade_layer(s, q, q.l0, 0, nm);
	s = shade_layer(s, q, q.l1, 1, nm);
	s = shade_layer(s, q, q.l2, 2, nm);
	s = shade_layer(s, q, q.l3, 3, nm);
	return s;
}

// what the rays of a part bring: off the wall (already read), through every body in front of it, the
// refracting one along their own chords of its liquid (c0 to c1; none, through its core as air), what its
// surfaces turn away showing the room in front; then what stands in front of it
float shaded(Shade s, Path q, float pass, float c0, float c1, bool liquid, float wall) {
	float seen = s.a_behind * wall + s.b_behind;
	if (s.own) {
		float depth = liquid ? s.per_cm * chord_of_part(c0, c1, s.per_cm) : 0.0;
		float through = exp(-depth);
		seen = seen * through;
		seen = mix(seen, s.colour * exp(-depth * 0.5), s.haze);
		seen = mix(seen, s.colour * through + s.glow * exp(-depth * 0.5), s.solid);
		seen = seen * s.glass;
	}
	if (q.refractor >= 0) {
		seen = pass * seen + (1.0 - pass) * s.room;
	}
	return s.a_front * seen + s.b_front;
}

float along(Path q, float nm, float pass, float c0, float c1, bool liquid, float wall) {
	return shaded(shade_of(q, nm), q, pass, c0, c1, liquid, wall);
}

// how fast the landing bends in wavelength, at most, in the whole pixel of the body that x is in (pixels of
// the screen per nm^2)
float map_bend(MapRef m, float x) {
	int c = int(floor(x - m.left));
	if (c < 0 || c >= m.coarse) {
		return 0.0;
	}
	return map_vec(m.at + 1 + c).y;
}

// how much of the pixel's height is in each core of its vessel: 0 its liquid, 1 the air over it, 2 its floor
float core_share(Path q, int core) {
	float wet = max(q.rin * q.rwet - q.rfloor, 0.0);
	return core == 0 ? wet : (core == 1 ? q.rin - q.rfloor - wet : q.rfloor);
}

// in how many straight steps a band of `nm` nanometres is swept, where the landing bends so: as many as keep
// the sweep within MAP_SWEEP_TOLERANCE (0.02 pixels of the screen) of the true landing; at most sixteen
int map_steps(float bend, float nm) {
	return int(clamp(ceil(nm * sqrt(bend / 0.16)), 1.0, 16.0));
}

// Between two places across a body whose rays go different ways (GAME_SPEC §12.3), `lo` and `hi` (pixels
// from its axis): where the map says the route changes (in a sample's cell, how far along: twice its
// route's fraction). A rim: all of a part inside the body is the ray's that goes through. Half way, if the
// map shows no change there.
float map_change(MapRef m, int medium, float lo, float hi, float nm) {
	float place = map_place(nm) * float(m.pieces);
	int piece = min(int(floor(place)), m.pieces - 1);
	float g = place - float(piece);
	int cells = m.at + 1 + m.coarse + (medium * m.samples * m.pieces + piece) * 4 + 3;
	int from = int(floor((lo - m.left) * MAP_FINE));
	for (int j = 0; j < 16; j++) {
		int i = from + j;
		if (i < 0 || i + 1 >= m.samples) {
			continue;
		}
		float here = nearest(map_vec(cells + i * m.pieces * 4), g); // at the nearest node
		if (floor(here) != floor(nearest(map_vec(cells + (i + 1) * m.pieces * 4), g))) {
			return m.left + (float(i) + 2.0 * fract(here)) / MAP_FINE;
		}
		if (m.left + float(i + 1) / MAP_FINE >= hi) {
			break;
		}
	}
	return 0.5 * (lo + hi);
}

vec4 share(float u);

// the wavelength at the share u of the light's energy; or `one_nm`, if one wavelength alone is asked for
float nm_at(float u, float one_nm) {
	return one_nm > 0.0 ? one_nm : share(u).x + 380.0;
}

// the share of the light's energy up to a wavelength: the table the other way
float share_of(float nm) {
	float x = clamp(nm - 380.0, 0.0, 400.0);
	int i = min(int(floor(x)), 399);
	return mix(light_vec(LIGHT_ENTRIES + i).x, light_vec(LIGHT_ENTRIES + i + 1).x, x - float(i));
}

// ---- beyond the reading for one vessel (GAME_SPEC §12.3)
//
// What a pass costs on the GPU follows how much there is in the shader a pixel could do, done or not
// (measured: with the general readings present, a pass in which every pixel is settled cost 0.58 ms, and
// without them nothing a frame could show). So the host compiles this file twice: without OPTICS_CHAIN for
// the pass over the whole screen, which leaves the pixels that need more alone; and with it for a pass over
// the rectangles of the bodies that can need it (a held vessel, a prism). The reference has both, and
// OPTICS_FOLLOW besides: the following of a ray through the layers, which runs on the CPU alone.

// what reaches the eye where no vessel bends the view: the bodies as they lie, before the backdrop
float flat_band(Path q, float u0, float u1, float t, float one_nm) {
	return along(q, nm_at(mix(u0, u1, t), one_nm), 1.0, 0.0, 0.0, false, q.back);
}

#ifdef OPTICS_CHAIN

// ---- one vessel, whatever its map (a prism's has many pieces): the general reading

// the ray at x and nm through the pixel's vessel, where it lands on the screen
Bend map_on(Path q, int medium, float x, float nm) {
	Bend b = map_at(ref_of(q), medium, x, nm);
	b.x = q.raxis + b.x;
	return b;
}

Bend edge_on(Path q, Edge e, float g) {
	Bend b = edge_at(e, g);
	b.x = q.raxis + b.x;
	return b;
}

// a ray's own side of a route's change, carried on to the change: from the ray `near` it, as far again as
// `t` of the way from the ray `far` on that side (the landing and what passes, straight; the chord by its square)
Bend toward(Bend near, Bend far, float t) {
	Bend b;
	b.x = near.x + (near.x - far.x) * t;
	b.pass = clamp(near.pass + (near.pass - far.pass) * t, 0.0, 1.0);
	b.chord = sqrt(max(near.chord * near.chord + (near.chord * near.chord - far.chord * far.chord) * t, 0.0));
	b.route = near.route;
	return b;
}

// the wall between two rays at one wavelength: the interval between their landings; or, if they go
// different ways, each ray's own landing, half and half
float end_wall(int profile, Bend l, Bend r) {
	if (l.route == r.route) {
		return wall_span(profile, l.x, r.x);
	}
	return 0.5 * (wall_span(profile, l.x, l.x) + wall_span(profile, r.x, r.x));
}

// One part of a pixel whose rays go different ways (GAME_SPEC §12.3), between the rays at `left` and
// `right` (pixels from the body's axis), read at a step's point nm: a and b are its left ray at the step's
// two ends, a2 and b2 its right. If the point's two rays go one way, the wall at the step's two ends; if
// not, each side is its own straight piece, from its end's ray to the change of route, continued from the
// ray beyond its end if that goes the same way.
float split_part(Path q, Shade shade, int profile, int medium, bool liquid, float left, float right, float nm, Bend a, Bend a2, Bend b, Bend b2) {
	Bend lp = map_on(q, medium, left, nm);
	Bend rp = map_on(q, medium, right, nm);
	if (lp.route == rp.route) {
		float wall = 0.5 * (end_wall(profile, a, a2) + end_wall(profile, b, b2)); // a route changes on the way: the step's two ends
		return shaded(shade, q, 0.5 * (lp.pass + rp.pass), lp.chord, rp.chord, liquid, wall);
	}
	float width = right - left;
	float share_left = clamp((map_change(ref_of(q), medium, left, right, nm) - left) / (right - left), 0.0, 1.0);
	Bend l_edge = lp;
	Bend beyond = map_on(q, medium, left - width, nm);
	if (beyond.route == lp.route) {
		l_edge = toward(lp, beyond, share_left);
	}
	Bend r_edge = rp;
	beyond = map_on(q, medium, right + width, nm);
	if (beyond.route == rp.route) {
		r_edge = toward(rp, beyond, 1.0 - share_left);
	}
	return share_left * shaded(shade, q, 0.5 * (lp.pass + l_edge.pass), lp.chord, l_edge.chord, liquid, wall_span(profile, lp.x, l_edge.x)) +
		   (1.0 - share_left) * shaded(shade, q, 0.5 * (rp.pass + r_edge.pass), r_edge.chord, rp.chord, liquid, wall_span(profile, r_edge.x, rp.x));
}

// a boundary ray, whose cell `e` of the map's piece `had` is in hand, at g of the way through `piece`
Bend edge_in(Path q, int medium, float x, Edge e, int had, int piece, float g) {
	if (piece == had) {
		return edge_on(q, e, g);
	}
	return edge_on(q, edge_load(ref_of(q), medium, x, piece), g);
}

// A pixel through one core of the body it looks through (its liquid, or air), over one straight step of a
// band's wavelengths, from `begin` to `end`, its point `nm` (GAME_SPEC §12.2, §12.3). The pixel is read in
// `parts` equal parts, from `left` (pixels from the body's axis). The rays of a part land in an interval of
// the wall that sweeps as the step is crossed, and the wall is read over that sweep exactly: the mean,
// along each boundary ray's sweep, of the row's integral (one boundary serves the parts on both its sides),
// the footprint's width taken as its mean. No edge on the wall is missed, in place or along the sweep.
// What the surfaces pass and the chords are the boundary rays' at the step's point. A boundary ray's cell
// of the map is read once for the step (a ray at every wavelength of its piece), and its place on the wall
// once, unless the sweep takes it out of its cell.
float core_light(Path q, Shade shade, int profile, int medium, bool liquid, float left, int parts, float begin, float end, float nm) {
	float place = map_place(begin) * float(q.rpieces);
	int piece_a = min(int(floor(place)), q.rpieces - 1);
	float g_a = place - float(piece_a);
	place = map_place(end) * float(q.rpieces);
	int piece_b = min(int(floor(place)), q.rpieces - 1);
	float g_b = place - float(piece_b);
	place = map_place(nm) * float(q.rpieces);
	int piece_p = min(int(floor(place)), q.rpieces - 1);
	float g_p = place - float(piece_p);
	Bend a0;
	Bend b0;
	Bend p0;
	WallAt origin;
	float m0 = 0.0;
	float sum = 0.0;
	for (int k = 0; k <= 4; k++) {
		if (k > parts) {
			break;
		}
		float x = left + float(k) / float(parts);
		Edge e = edge_load(ref_of(q), medium, x, piece_a);
		Bend a = edge_on(q, e, g_a);
		Bend b = edge_in(q, medium, x, e, piece_a, piece_b, g_b);
		Bend p = edge_in(q, medium, x, e, piece_a, piece_p, g_p);
		WallAt wall_a = wall_at(profile, a.x);
		WallAt wall_b = wall_move(profile, wall_a, b.x);
		if (k == 0) {
			origin = wall_a;
		}
		float m = wall_mean(profile, origin, wall_a, a.x, wall_b, b.x);
		if (k > 0) {
			if (a0.route == a.route && b0.route == b.route && a0.route == b0.route) {
				float w0 = a.x - a0.x;
				float w1 = b.x - b0.x;
				float wall = 0.0;
				if (w0 * w1 < 0.0) {
					// folded over on the way: each end's own footprint
					wall = 0.5 * (wall_span(profile, a0.x, a.x) + wall_span(profile, b0.x, b.x));
				} else if (abs(w0 + w1) < 2.0e-4) {
					// no width: one ray, moving
					wall = wall_span(profile, 0.5 * (a0.x + a.x), 0.5 * (b0.x + b.x));
				} else {
					wall = (m - m0) / (0.5 * (w0 + w1));
				}
				sum = sum + shaded(shade, q, 0.5 * (p0.pass + p.pass), p0.chord, p.chord, liquid, wall);
			} else {
				sum = sum + split_part(q, shade, profile, medium, liquid, left + float(k - 1) / float(parts), x, nm, a0, a, b0, b);
				// the integral from here on is counted from this boundary: the two sides may be far apart on the wall
				origin = wall_a;
				m = wall_mean(profile, origin, wall_a, a.x, wall_b, b.x);
			}
		}
		a0 = a;
		b0 = b;
		p0 = p;
		m0 = m;
	}
	return sum / float(parts);
}

// The share of the room's light that reaches the eye over a band of its energy, from the share u0 to u1,
// its point t of the way through (GAME_SPEC §12.2, §12.3); or at one wavelength, `one_nm`, if that is asked
// for. A pixel is the light of all its rays: through a refracting body it is read in as many equal parts as
// the body's map is straight within; a row across its liquid's surface is its two cores' lights, each by
// its share of the pixel's height; and a row across its top or floor, the bent view and the straight one
// likewise. The band is swept in straight steps, even in wavelength, along which a landing moves smoothly,
// each weighed by its own share of the band's energy (a band is even in energy, not in wavelength: its red
// end may be all tail). In each step the liquids and the surfaces are read at the step's own point (t of
// the way through it): so what a bright edge brings is weighed by what gets through at the wavelength it
// is crossed at.
float general_band(Path q, float u0, float u1, float t, float one_nm) {
	float nm0 = nm_at(u0, one_nm);
	float nm1 = nm_at(u1, one_nm);
	if (q.refractor < 0) {
		return flat_band(q, u0, u1, t, one_nm);
	}
	int profile = int(wall_vec(WALL_ROW_BASE + clamp(int(floor(q.y)), 0, WALL_ROWS - 1)).x);
	float left = q.x - 0.5 - q.raxis;
	int parts = int(max(map_level(ref_of(q), left), map_level(ref_of(q), left + 0.9999)));
	float across = abs(nm1 - nm0);
	int steps = map_steps(max(map_bend(ref_of(q), left), map_bend(ref_of(q), left + 0.9999)), across);
	float total = 0.0;
	float weights = 0.0;
	float begin = nm0;
	float u_begin = steps > 1 ? share_of(nm0) : 0.0;
	for (int s = 1; s <= 16; s++) {
		if (s > steps) {
			break;
		}
		float end = mix(nm0, nm1, float(s) / float(steps));
		float nm = steps > 1 ? mix(begin, end, t) : nm_at(mix(u0, u1, t), one_nm);
		float u_end = steps > 1 ? share_of(end) : 1.0;
		float weight = max(u_end - u_begin, 0.0);
		Shade shade = shade_of(q, nm);
		float light = 0.0;
		for (int core = 0; core < 3; core++) {
			float share_core = core_share(q, core);
			if (share_core <= 0.0) {
				continue;
			}
			light = light + share_core * core_light(q, shade, profile, q.rmedia > 1 ? core : 0, core == 0, left, parts, begin, end, nm);
		}
		if (q.rin < 1.0) {
			light = light + (1.0 - q.rin) * shaded(shade, q, 1.0, 0.0, 0.0, false, wall_span(profile, q.x - 0.5, q.x + 0.5));
		}
		total = total + weight * light;
		weights = weights + weight;
		begin = end;
		u_begin = u_end;
	}
	return weights > 0.0 ? total / weights : 0.0;
}

// ---- a stack (GAME_SPEC §12.3, Stage 21): a pixel through several bodies
//
// The rays of a vessel with bodies behind it are followed on the CPU (lab/optics_scene.hpp: the stacks), as
// a vessel's map is traced there: a ray at every sample across the vessel (four a pixel, on the screen's
// own quarter pixels), in each band of rows in which every body is one thing, at the four nodes of the
// spectrum, layer by layer through the bodies' maps. A pixel reads those as it reads a map: between the
// nodes a ray is the cubic through the four, or its nearest node's where its route differs among them.
// Nothing here follows a ray: following one in the pass cost 344 ms a frame for one held vessel.
//
// The includer's stack_vec lays them out:
//   [b], b < 16            where body b's stack begins (x), or -1: its pixels are read as one vessel's
//   and from a stack's start o:
//   [o]                    the whole pixels it spans, its bands, its samples, its slots
//   [o + 1]                its first sample's place on the screen (x)
//   [o + 2 + k]            band k: its top and its bottom (rows of the screen)
//   [o + 2 + bands + j]    slot j: its body (x), whether that body bends (y); the nearest the eye first
//   then, by band and sample, a cell of 2 + 4 * slots vec4s, each at the four nodes: where the ray lands (on
//   the screen); its route (a whole number, and half of how far along to the next sample the route
//   changes); and for each slot what the ray did there (0: its line never reached the body's layer; 1: it
//   crossed the layer's plane; 2: the body bent it), where it crossed the plane, what the body's surfaces
//   passed, its chord in the body's liquid, squared.

// The wall over a part's footprint as a step of a band's wavelengths is crossed: its two rays land at a0 and
// a at the step's beginning, at b0 and b at its end. The mean, along each ray's sweep, of the row's integral,
// the footprint's width taken as its mean: exact, however many edges the sweep crosses.
float swept_wall(int profile, float a0, float a, float b0, float b) {
	float w0 = a - a0;
	float w1 = b - b0;
	if (w0 * w1 < 0.0) {
		// folded over on the way: each end's own footprint
		return 0.5 * (wall_span(profile, a0, a) + wall_span(profile, b0, b));
	}
	if (abs(w0 + w1) < 2.0e-4) {
		// no width: one ray, moving
		return wall_span(profile, 0.5 * (a0 + a), 0.5 * (b0 + b));
	}
	WallAt origin = wall_at(profile, a0);
	WallAt wall_a = wall_move(profile, origin, a);
	float m0 = wall_mean(profile, origin, origin, a0, wall_move(profile, origin, b0), b0);
	float m = wall_mean(profile, origin, wall_a, a, wall_move(profile, wall_a, b), b);
	return (m - m0) / (0.5 * (w0 + w1));
}

// What one body does to the light that reaches it from behind, at one wavelength: it takes it to x times it
// plus y. Through `depth` (optical) of its liquid; its haze turns the room's light back from half way in; a
// heap in it is lit and seen through it, and glows (`glow`, a multiple of the room's light) through half of
// it; its wall; and its surfaces pass `pass` of all that, the rest showing the room in front.
vec2 body_does(Layer l, float colour, float glow, float skin_depth, float depth, float pass, float room) {
	float skin = exp(-skin_depth * l.wall);
	float through = exp(-depth);
	float a = through * (1.0 - l.haze) * (1.0 - l.solid) * skin;
	float b = (colour * (exp(-depth * 0.5) * l.haze * (1.0 - l.solid) + through * l.solid) + glow * exp(-depth * 0.5) * l.solid) * skin;
	return vec2(pass * a, pass * b + (1.0 - pass) * room);
}

vec4 whole_of(vec4 r) {
	return vec4(floor(r.x), floor(r.y), floor(r.z), floor(r.w));
}

// how fast a ray's landing bends in the wavelength, at most (pixels of the screen per nm^2): from its nodes
float land_bend(vec4 land, vec4 route) {
	vec4 r = whole_of(route);
	if (r.y != r.x || r.z != r.x || r.w != r.x) {
		return 0.0; // its nearest node's ray: it does not sweep
	}
	float nm1 = 380.0 / sqrt(1.0 - 0.7626561473 / 3.0);
	float nm2 = 380.0 / sqrt(1.0 - 0.7626561473 * 2.0 / 3.0);
	float first = ((land.z - land.y) / (nm2 - nm1) - (land.y - land.x) / (nm1 - 380.0)) / (nm2 - 380.0);
	float second = ((land.w - land.z) / (780.0 - nm2) - (land.z - land.y) / (nm2 - nm1)) / (780.0 - nm1);
	return 2.0 * max(abs(first), abs(second));
}

// one value of a stack's ray at a wavelength: its cell's vec4 at `at`, by the weights w; carried on by `on`
// of the way again from the ray beside it (`far`, by its weights)
float stack_value(int at, vec4 w, int far, vec4 w_far, float on) {
	float v = dot(w, stack_vec(at));
	return v + (v - dot(w_far, stack_vec(far))) * on;
}

// What the rays between two rays of a stack bring (GAME_SPEC §12.3): off the wall (already read over their
// footprint), through every body they met, gathered from the eye inward: each body takes what reaches it to
// a times it plus b. Ray A is the cell `a` by the weights w_a, carried on by on_a from the cell far_a; ray B
// likewise. A body is read where the rays cross it: its outline, its heap and its haze at the middle of
// their crossings (a sample as wide as they are apart); the liquid of one that bent them along their own
// chords; what its surfaces turned away shows the room in front. What they met is cell `met`'s, at the
// nearest node.
float stack_light(Path q, int slots_at, int slots, int a, vec4 w_a, int far_a, vec4 w_far_a, float on_a, int b, vec4 w_b, int far_b, vec4 w_far_b, float on_b, int met, vec4 near, float nm, float wall) {
	float gain = 1.0;
	float added = 0.0;
	float room = scene_vec(2).y;
	for (int j = 0; j < 6; j++) {
		if (j >= slots) {
			break;
		}
		int at = 2 + 4 * j;
		float kind = dot(near, stack_vec(met + at));
		vec4 slot = stack_vec(slots_at + j);
		if (kind < 0.5 || (slot.y > 0.5 && kind < 1.5)) {
			continue; // not reached; or a body that bends, and these rays pass beside it or over it
		}
		int body = int(slot.x);
		float cross_a = stack_value(a + at + 1, w_a, far_a + at + 1, w_far_a, on_a);
		float cross_b = stack_value(b + at + 1, w_b, far_b + at + 1, w_far_b, on_b);
		Layer l = layer_at(body, vec2(0.5 * (cross_a + cross_b), q.y), max(abs(cross_b - cross_a), 1.0e-3 * q.size), q.size);
		int start = body * ROWS_PER_BODY * ROW;
		float per_cm = liquid_at(body, nm);
		float depth = per_cm * l.depth_cm;
		float pass = 1.0;
		if (kind > 1.5) {
			float chord_a = sqrt(max(stack_value(a + at + 3, w_a, far_a + at + 3, w_far_a, on_a), 0.0));
			float chord_b = sqrt(max(stack_value(b + at + 3, w_b, far_b + at + 3, w_far_b, on_b), 0.0));
			depth = per_cm * chord_of_part(chord_a, chord_b, per_cm);
			pass = 0.5 * (clamp(stack_value(a + at + 2, w_a, far_a + at + 2, w_far_a, on_a), 0.0, 1.0) + clamp(stack_value(b + at + 2, w_b, far_b + at + 2, w_far_b, on_b), 0.0, 1.0));
		}
		vec2 does = body_does(l, row_at(start + ROW, nm), row_at(start + 5 * ROW, nm), row_at(start + 2 * ROW, nm), depth, pass, room);
		added = added + gain * does.y;
		gain = gain * does.x;
	}
	return gain * wall + added;
}

// the wall between two rays at one wavelength: the interval between their landings; or, if they go
// different ways, each ray's own landing, half and half
float ends_wall(int profile, float l, float r, bool one_way) {
	if (one_way) {
		return wall_span(profile, l, r);
	}
	return 0.5 * (wall_span(profile, l, l) + wall_span(profile, r, r));
}

// One band of a stack's rows, for a pixel whose left edge is the sample of the cell `cell` (`index` among
// the band's `samples`), over a band of the light's energy from the share u0 to u1, its point t of the way
// through; or at one wavelength. In four parts across (a level for each pixel, as a map has, saved nothing
// on the GPU: 13.1 ms a pass against 14.2). The wall is read over each part's footprint as the band is
// swept, in straight steps even in wavelength along which a landing moves smoothly, each weighed by its own
// share of the band's energy, as one vessel's pixel is read. The bodies are read once, at the band's point:
// a stack's rays spread the wavelengths wide, a band is swept in many steps, and what a step costs is the
// bodies (measured: the fixed estimate's eight wide bands cost as much as sixty-four narrow ones).
float stack_rows(Path q, int profile, int cell, int stride, int index, int samples, int slots_at, int slots, float u0, float u1, float t, float one_nm) {
	float nm0 = nm_at(u0, one_nm);
	float nm1 = nm_at(u1, one_nm);
	float nm = nm_at(mix(u0, u1, t), one_nm);
	float bend = 0.0;
	for (int k = 0; k <= 4; k++) {
		bend = max(bend, land_bend(stack_vec(cell + k * stride), stack_vec(cell + k * stride + 1)));
	}
	int steps = map_steps(bend, abs(nm1 - nm0));
	float g_p = map_place(nm);
	vec4 cubic_p = thirds_weights(g_p);
	vec4 near_p = nearest_weights(g_p);
	float sum = 0.0;
	for (int k = 0; k < 4; k++) {
		int c0 = cell + k * stride;
		int c1 = c0 + stride;
		vec4 land0 = stack_vec(c0);
		vec4 land1 = stack_vec(c1);
		vec4 turn0 = stack_vec(c0 + 1);
		vec4 r0 = whole_of(turn0);
		vec4 r1 = whole_of(stack_vec(c1 + 1));
		bool whole0 = r0.y == r0.x && r0.z == r0.x && r0.w == r0.x;
		bool whole1 = r1.y == r1.x && r1.z == r1.x && r1.w == r1.x;
		vec4 w_p0 = whole0 ? cubic_p : near_p;
		vec4 w_p1 = whole1 ? cubic_p : near_p;
		float way_p0 = dot(near_p, r0);
		float way_p1 = dot(near_p, r1);
		if (way_p0 == way_p1) {
			// the two rays go one way at the band's point: the wall over the footprint's sweep; or, in a step
			// where a route changes on the way, at the step's two ends
			float wall = 0.0;
			float weights = 0.0;
			float begin = nm0;
			float u_begin = steps > 1 ? share_of(nm0) : 0.0;
			float g_a = map_place(begin);
			vec4 near_a = nearest_weights(g_a);
			vec4 cubic_a = thirds_weights(g_a);
			for (int step = 1; step <= 16; step++) {
				if (step > steps) {
					break;
				}
				float end = mix(nm0, nm1, float(step) / float(steps));
				float u_end = steps > 1 ? share_of(end) : 1.0;
				float weight = max(u_end - u_begin, 0.0);
				float g_b = map_place(end);
				vec4 near_b = nearest_weights(g_b);
				vec4 cubic_b = thirds_weights(g_b);
				float xa0 = dot(whole0 ? cubic_a : near_a, land0);
				float xb0 = dot(whole0 ? cubic_b : near_b, land0);
				float xa1 = dot(whole1 ? cubic_a : near_a, land1);
				float xb1 = dot(whole1 ? cubic_b : near_b, land1);
				float way_a0 = dot(near_a, r0);
				float way_b0 = dot(near_b, r0);
				float way_a1 = dot(near_a, r1);
				float way_b1 = dot(near_b, r1);
				wall = wall + weight * (way_a0 == way_a1 && way_b0 == way_b1 && way_a0 == way_b0 ? swept_wall(profile, xa0, xa1, xb0, xb1)
											   : 0.5 * (ends_wall(profile, xa0, xa1, way_a0 == way_a1) + ends_wall(profile, xb0, xb1, way_b0 == way_b1)));
				weights = weights + weight;
				begin = end;
				u_begin = u_end;
				near_a = near_b;
				cubic_a = cubic_b;
			}
			wall = weights > 0.0 ? wall / weights : wall_span(profile, dot(w_p0, land0), dot(w_p1, land1));
			sum = sum + stack_light(q, slots_at, slots, c0, w_p0, c0, w_p0, 0.0, c1, w_p1, c1, w_p1, 0.0, c0, near_p, nm, wall);
		} else {
			// they part ways: each side its own straight piece, from its ray to the change (the cell says
			// how far along), carried on from the ray beside it if that goes the same way
			float left = clamp(2.0 * fract(dot(near_p, turn0)), 0.0, 1.0);
			float xp0 = dot(w_p0, land0);
			float xp1 = dot(w_p1, land1);
			int beside = index + k > 0 ? c0 - stride : c0;
			vec4 rb = whole_of(stack_vec(beside + 1));
			vec4 w_beside = rb.y == rb.x && rb.z == rb.x && rb.w == rb.x ? cubic_p : near_p;
			float on = beside != c0 && dot(near_p, rb) == way_p0 ? left : 0.0;
			float edge = xp0 + (xp0 - dot(w_beside, stack_vec(beside))) * on;
			sum = sum + left * stack_light(q, slots_at, slots, c0, w_p0, c0, w_p0, 0.0, c0, w_p0, beside, w_beside, on, c0, near_p, nm, wall_span(profile, xp0, edge));
			beside = index + k + 2 < samples ? c1 + stride : c1;
			rb = whole_of(stack_vec(beside + 1));
			w_beside = rb.y == rb.x && rb.z == rb.x && rb.w == rb.x ? cubic_p : near_p;
			on = beside != c1 && dot(near_p, rb) == way_p1 ? 1.0 - left : 0.0;
			edge = xp1 + (xp1 - dot(w_beside, stack_vec(beside))) * on;
			sum = sum + (1.0 - left) * stack_light(q, slots_at, slots, c1, w_p1, beside, w_beside, on, c1, w_p1, c1, w_p1, 0.0, c1, near_p, nm, wall_span(profile, edge, xp1));
		}
	}
	return 0.25 * sum;
}

// whether the pixel's vessel has a stack, and the pixel is within it: its rays were followed
bool stacked(Path q) {
	if (!q.chain) {
		return false;
	}
	int o = int(stack_vec(q.refractor).x);
	if (o < 0) {
		return false;
	}
	int index = int(floor((q.x - 0.5 - stack_vec(o + 1).x) * MAP_FINE + 0.5));
	return index >= 0 && index + 4 < int(stack_vec(o).z);
}

// A band of the light through a stack: each band of the stack's rows the pixel's height is in, by its share
// (a row across a liquid's surface is two bands' lights; a row across a top or a floor, likewise).
float stack_band(Path q, float u0, float u1, float t, float one_nm) {
	int o = int(stack_vec(q.refractor).x);
	vec4 head = stack_vec(o);
	int bands = int(head.y);
	int samples = int(head.z);
	int slots = int(head.w);
	int slots_at = o + 2 + bands;
	int cells_at = slots_at + slots;
	int cell = 2 + 4 * slots; // vec4s
	int index = int(floor((q.x - 0.5 - stack_vec(o + 1).x) * MAP_FINE + 0.5));
	int profile = int(wall_vec(WALL_ROW_BASE + clamp(int(floor(q.y)), 0, WALL_ROWS - 1)).x);
	float top = q.y - 0.5 * q.size;
	float bottom = q.y + 0.5 * q.size;
	float light = 0.0;
	float covered = 0.0;
	for (int k = 0; k < 64; k++) {
		if (k >= bands) {
			break;
		}
		vec4 rows = stack_vec(o + 2 + k);
		float share = (min(bottom, rows.y) - max(top, rows.x)) / q.size;
		if (share > 0.0) {
			light = light + share * stack_rows(q, profile, cells_at + (k * samples + index) * cell, cell, index, samples, slots_at, slots, u0, u1, t, one_nm);
			covered = covered + share;
		}
	}
	return covered > 0.0 ? light / covered : flat_band(q, u0, u1, t, one_nm);
}

// what a pixel the reading for one vessel does not take gets
float other_band(Path q, float u0, float u1, float t, float one_nm) {
	if (q.refractor < 0) {
		return flat_band(q, u0, u1, t, one_nm);
	}
	if (stacked(q)) {
		return stack_band(q, u0, u1, t, one_nm);
	}
	return general_band(q, u0, u1, t, one_nm);
}

#else

// the pass over the whole screen: no pixel that needs more than one vessel's reading is read by it
float other_band(Path q, float u0, float u1, float t, float one_nm) {
	return flat_band(q, u0, u1, t, one_nm);
}

#endif

#ifdef OPTICS_FOLLOW

// ---- a ray followed through the layers (GAME_SPEC §12.3, Stage 21): on the CPU, where a stack is made

const int MAX_VISITS = 6; // the bodies one pixel's rays are followed through
const int MAX_STRIPS = 4; // the strips a pixel's height is read in, where bodies' tops, floors and surfaces cut it

// The bodies a pixel's rays may meet, the nearest the eye first (a nearer layer's before a further one's;
// in a layer, the last painted first): those the pixel looks straight at, and those of the layers behind
// its vessel that its bent rays can reach. The screen is one world in pixels: a layer's plane is its number
// of gaps in front of the bench's, and the wall is behind the bench's. And the strips of the pixel's height
// between the tops, floors and surfaces of those that bend: in each, every body is one thing.
struct Stack {
	int count;
	int body[MAX_VISITS];
	float layer[MAX_VISITS];
	bool bends[MAX_VISITS];
	bool round[MAX_VISITS]; // the same from every side: its map answers a ray from anywhere
	float axis[MAX_VISITS]; // on the screen
	float radius[MAX_VISITS];
	float top[MAX_VISITS];
	float foot[MAX_VISITS];
	float surface[MAX_VISITS];
	float base[MAX_VISITS]; // its floor's top (its foot, with no floor)
	int map[MAX_VISITS];
	int coarse[MAX_VISITS];
	int samples[MAX_VISITS];
	int pieces[MAX_VISITS];
	int media[MAX_VISITS];
	float left[MAX_VISITS];
	bool chained; // more than one of them bends
	int first; // the nearest that bends
	int strips;
	float strip_y[MAX_STRIPS];
	float strip_share[MAX_STRIPS];
	float wall; // behind the bench's plane (pixels)
	float gap; // between two layers' planes
};

MapRef ref_at(Stack s, int j) {
	MapRef m;
	m.at = s.map[j];
	m.coarse = s.coarse[j];
	m.samples = s.samples[j];
	m.pieces = s.pieces[j];
	m.media = s.media[j];
	m.left = s.left[j];
	return m;
}

// a pixel's stack; an empty one where it is not asked for (a pixel read by the reading for one vessel)
Stack stack_of(Path q, bool wanted) {
	Stack s;
	s.count = 0;
	s.chained = false;
	s.first = 0;
	s.strips = 1;
	s.wall = scene_vec(2).x;
	s.gap = scene_vec(3).x;
	for (int j = 0; j < MAX_VISITS; j++) {
		s.body[j] = 0;
		s.layer[j] = 0.0;
		s.bends[j] = false;
		s.round[j] = false;
		s.axis[j] = 0.0;
		s.radius[j] = 0.0;
		s.top[j] = 0.0;
		s.foot[j] = 0.0;
		s.surface[j] = 0.0;
		s.base[j] = 0.0;
		s.map[j] = 0;
		s.coarse[j] = 0;
		s.samples[j] = 0;
		s.pieces[j] = 1;
		s.media[j] = 1;
		s.left[j] = 0.0;
	}
	for (int n = 0; n < MAX_STRIPS; n++) {
		s.strip_y[n] = q.y;
		s.strip_share[n] = n == 0 ? 1.0 : 0.0;
	}
	if (!wanted) {
		return s;
	}
	int count = int(scene_vec(0).x);
	int bending = 0;
	for (int k = 0; k < 16; k++) {
		int i = count - 1 - k;
		if (i < 0 || s.count >= MAX_VISITS) {
			break;
		}
		int at = HEADER_VECS + i * BODY_VECS;
		vec4 rect = scene_vec(at);
		if (q.y < rect.y - 1.0 || q.y > rect.y + rect.w + 1.0) {
			continue;
		}
		vec4 refract = scene_vec(at + 5);
		float centre = rect.x + rect.z * 0.5;
		bool here = q.x >= rect.x - 1.0 && q.x <= rect.x + rect.z + 1.0;
		// of a layer behind the pixel's vessel, and within two gaps' reach a layer of its bent rays
		bool beyond = refract.y < q.rlayer && abs(q.x - centre) <= rect.z * 0.5 + 2.0 * s.gap * (q.rlayer - refract.y);
		if (!here && !beyond) {
			continue;
		}
		vec4 form = scene_vec(at + 1);
		int j = s.count;
		s.body[j] = i;
		s.layer[j] = refract.y;
		s.bends[j] = refract.w > 0.5 && (form.x < 0.5 || form.x > 1.5);
		s.round[j] = form.x < 0.5;
		s.axis[j] = form.x > 1.5 ? centre + scene_vec(at + 3).z : centre;
		s.radius[j] = rect.z * 0.5;
		s.top[j] = rect.y;
		s.foot[j] = rect.y + rect.w;
		s.surface[j] = form.z;
		s.base[j] = s.foot[j] - refract.z / form.y;
		if (s.bends[j]) {
			vec4 map = scene_vec(at + 6);
			vec4 map_head = map_vec(int(map.x));
			s.map[j] = int(map.x);
			s.coarse[j] = int(map.y);
			s.left[j] = map.z;
			s.pieces[j] = int(map.w);
			s.media[j] = int(map_head.y);
			s.samples[j] = int(map_head.z);
			if (bending == 0) {
				s.first = j;
			}
			bending = bending + 1;
		}
		s.count = j + 1;
	}
	s.chained = bending > 1;
	// the strips: from the pixel's top down, each to the next top, floor or surface of a body that bends
	float lo = q.y - 0.5 * q.size;
	float end = q.y + 0.5 * q.size;
	s.strips = 0;
	for (int n = 0; n < MAX_STRIPS; n++) {
		float hi = end;
		if (n < MAX_STRIPS - 1) {
			for (int j = 0; j < MAX_VISITS; j++) {
				if (j >= s.count) {
					break;
				}
				if (s.bends[j]) {
					hi = s.top[j] > lo + 1.0e-5 && s.top[j] < hi ? s.top[j] : hi;
					hi = s.foot[j] > lo + 1.0e-5 && s.foot[j] < hi ? s.foot[j] : hi;
					hi = s.surface[j] > lo + 1.0e-5 && s.surface[j] < hi ? s.surface[j] : hi;
					hi = s.base[j] > lo + 1.0e-5 && s.base[j] < hi ? s.base[j] : hi;
				}
			}
		}
		s.strip_y[n] = 0.5 * (lo + hi);
		s.strip_share[n] = (hi - lo) / q.size;
		s.strips = n + 1;
		lo = hi;
		if (hi >= end - 1.0e-5) {
			break;
		}
	}
	return s;
}

// which core of a body a row is in: 0 its liquid (or the one core of a body with one), 1 the air over it,
// 2 its floor; -1, past its top or its foot
int core_of(Stack s, int j, float y) {
	if (y < s.top[j] || y > s.foot[j]) {
		return -1;
	}
	if (s.media[j] > 2 && y >= s.base[j]) {
		return 2;
	}
	return s.media[j] > 1 && y < s.surface[j] ? 1 : 0;
}

// One ray, followed from the eye to the wall: where it lands (on the screen), its route (the same for rays
// that go the same way), and what it met, by the stack's bodies: whether its line reached the body's layer,
// where it crossed the layer's plane as it arrived there, how far from the body's axis it passed, and, if
// the body bent it, what the body's surfaces passed and its chord in the body's liquid (cm).
struct Ray {
	float x;
	float route;
	bool seen[MAX_VISITS];
	bool bent[MAX_VISITS];
	float cross[MAX_VISITS];
	float from_axis[MAX_VISITS];
	float way[MAX_VISITS]; // its route in that body's map
	float pass[MAX_VISITS];
	float chord[MAX_VISITS];
};

// The ray that comes straight in at x (on the screen), in the row y, at a wavelength. Layer by layer, the
// nearest first: every body of the layer is read where the ray's line, as it arrives, crosses the layer's
// plane; the layer's body that bends, the first the line meets, turns it. A round body is the same from
// every side, so the ray leaves as it came, turned about the body's axis by what its map's ray is turned
// (L cos d - W sin d = b: b from the axis, landing at L on a wall W behind it). A body that is not round is
// read as it is seen straight on, where the line crosses its plane. A ray is not followed into a second
// body of one layer.
Ray chain_at(Stack s, float y, float x, float nm) {
	Ray r;
	r.x = x;
	r.route = 0.0;
	for (int j = 0; j < MAX_VISITS; j++) {
		r.seen[j] = false;
		r.bent[j] = false;
		r.cross[j] = x;
		r.from_axis[j] = 0.0;
		r.way[j] = 0.0;
		r.pass[j] = 1.0;
		r.chord[j] = 0.0;
	}
	// its line: through (px, pz), heading a from straight in (z is away from the eye, 0 the bench's plane)
	float px = x;
	float pz = 0.0;
	float a = 0.0;
	bool lands = true;
	int turns = 0;
	float direct = x; // where its one body's map lands it, if it came to that body straight
	float cross = x;
	int hit = -1;
	for (int j = 0; j < MAX_VISITS; j++) {
		if (j >= s.count || !lands) {
			break;
		}
		float z = -s.layer[j] * s.gap;
		if (j == 0 || s.layer[j] != s.layer[j - 1]) {
			// a new layer: where the line crosses its plane, and which of its bodies that bend it meets first
			cross = px + (z - pz) * sin(a) / cos(a);
			hit = -1;
			float first = 1.0e30;
			for (int k = 0; k < MAX_VISITS; k++) {
				if (k < j) {
					continue;
				}
				if (k >= s.count || s.layer[k] != s.layer[j]) {
					break;
				}
				if (!s.bends[k] || core_of(s, k, y) < 0) {
					continue;
				}
				float b = s.round[k] ? (cross - s.axis[k]) * cos(a) : cross - s.axis[k];
				r.from_axis[k] = b;
				if (abs(b) > s.radius[k] + 1.0) {
					continue; // past its map: the map itself says where its outline is
				}
				float along = (s.axis[k] - cross) * sin(a) - sqrt(max(s.radius[k] * s.radius[k] - b * b, 0.0));
				if (along < first) {
					first = along;
					hit = k;
				}
			}
		}
		r.seen[j] = true;
		r.cross[j] = cross;
		if (j != hit) {
			continue;
		}
		int core = core_of(s, j, y);
		float b = r.from_axis[j];
		Bend bend = map_at(ref_at(s, j), s.media[j] > 1 ? core : 0, b, nm);
		if (bend.route < 0.5) {
			continue; // past its outline after all
		}
		float wall = s.wall + s.layer[j] * s.gap; // behind its own plane
		r.bent[j] = true;
		r.way[j] = bend.route;
		r.pass[j] = bend.pass;
		r.chord[j] = core == 0 && s.media[j] > 1 ? bend.chord : 0.0;
		r.route = r.route * 1.37 + 16.0 * float(j + 1) + bend.route;
		if (abs(bend.x) > 1.0e4) {
			// it leaves sideways or back toward the eye: far off the wall, on the side it goes
			lands = false;
			r.x = bend.x;
			break;
		}
		if (turns == 0 && a == 0.0) {
			direct = s.axis[j] + bend.x;
		}
		turns = turns + 1;
		if (s.round[j]) {
			a = a + atan(bend.x, wall) - asin(clamp(b / sqrt(bend.x * bend.x + wall * wall), -1.0, 1.0));
			px = s.axis[j] + b * cos(a);
			pz = z - b * sin(a);
		} else {
			a = a + atan(bend.x - b, wall);
			px = cross;
			pz = z;
		}
		if (cos(a) < 0.05) {
			lands = false;
			r.x = sin(a) < 0.0 ? -1.0e5 : 1.0e5;
		}
	}
	if (lands) {
		// through one body it came to straight, where that body's map lands it, to the last bit
		r.x = turns == 1 && direct != x ? direct : (turns == 0 ? x : px + (s.wall - pz) * sin(a) / cos(a));
	} else {
		r.route = r.route + 0.5;
	}
	return r;
}

#endif

// ---- a pixel through a map of one piece (GAME_SPEC §12.3)

// A vessel's map is one piece: a boundary ray is one cell at every wavelength. And a map's samples lie on
// the screen's own sixteenths of a pixel, so a pixel's parts begin and end on samples. Such a pixel is read
// by the arithmetic of any other (through_band), with nothing of what it never needs: what a pass costs on
// the GPU follows how much there is to do in a band, done or not.
struct Plain {
	bool ok;
	int profile; // its row's profile of the wall
	int parts;
	int cell; // where its left boundary ray's cell is among the maps, through the body's first core
	int first; // which of the map's samples that ray is
	int stride; // from a boundary ray's cell to the next one's
	float bend;
	float straight; // the wall straight through, for the share of the pixel past the body's top or floor
};

Plain plain_of(Path q) {
	Plain p;
	p.ok = false;
	p.profile = 0;
	p.parts = 1;
	p.cell = 0;
	p.first = 0;
	p.stride = 0;
	p.bend = 0.0;
	p.straight = 0.0;
	if (q.refractor < 0 || q.rpieces != 1 || q.chain) {
		return p;
	}
	float left = q.x - 0.5 - q.raxis;
	float s = (left - q.rleft) * MAP_FINE;
	if (s < 0.0 || abs(s - floor(s + 0.5)) > 1.0e-3 || s + MAP_FINE > float(q.rsamples) - 0.5) {
		return p;
	}
	p.profile = int(wall_vec(WALL_ROW_BASE + clamp(int(floor(q.y)), 0, WALL_ROWS - 1)).x);
	p.parts = int(map_level(ref_of(q), left + 0.5));
	p.bend = map_bend(ref_of(q), left + 0.5);
	p.stride = (int(MAP_FINE) / p.parts) * 4;
	p.first = int(floor(s + 0.5));
	p.cell = q.rmap + 1 + q.rcoarse + p.first * 4;
	if (q.rin < 1.0) {
		p.straight = wall_span(p.profile, q.x - 0.5, q.x + 0.5);
	}
	p.ok = true;
	return p;
}


// the wall a single ray brings as it sweeps from one place to another: the mean between them
float wall_along(WallAt a, float xa, WallAt b, float xb) {
	return abs(xb - xa) < 1.0e-4 ? a.radiance : wall_between(a, b) / (xb - xa);
}

// One side of a route's change within a part (a rim): a ray's own side, from the ray to the change, which is
// `share` of a part away. What passes and the chord are carried on from the ray's own as its neighbour on
// that side puts them (the ray `beyond`, a part away, if it goes the same way), the chord by its square (at
// a vessel's rim it falls to nothing, as a root). The wall is read where the ray itself lands, along its
// sweep: its side is a quarter of a pixel at most, of rays that graze a surface and bring little.
float side_light(Path q, Shade shade, bool liquid, WallAt at_a, float xa, WallAt at_b, float xb, float pass, float chord, float beyond_pass, float beyond_chord, bool same_way, float share) {
	float on = same_way ? share : 0.0;
	float pass_there = clamp(pass + (pass - beyond_pass) * on, 0.0, 1.0);
	float chord_there = sqrt(max(chord * chord + (chord * chord - beyond_chord * beyond_chord) * on, 0.0));
	return shaded(shade, q, 0.5 * (pass + pass_there), chord, chord_there, liquid, wall_along(at_a, xa, at_b, xb));
}

// through_band, through a map of one piece.
float plain_band(Path q, Plain p, float u0, float u1, float t, float one_nm) {
	float nm0 = nm_at(u0, one_nm);
	float nm1 = nm_at(u1, one_nm);
	int steps = map_steps(p.bend, abs(nm1 - nm0));
	float total = 0.0;
	float weights = 0.0;
	float begin = nm0;
	float u_begin = steps > 1 ? share_of(nm0) : 0.0;
	for (int s = 1; s <= 16; s++) {
		if (s > steps) {
			break;
		}
		float end = mix(nm0, nm1, float(s) / float(steps));
		float nm = steps > 1 ? mix(begin, end, t) : nm_at(mix(u0, u1, t), one_nm);
		float u_end = steps > 1 ? share_of(end) : 1.0;
		float weight = max(u_end - u_begin, 0.0);
		Shade shade = shade_of(q, nm);
		float g_a = map_place(begin);
		float g_b = map_place(end);
		float g_p = map_place(nm);
		vec4 cubic_a = thirds_weights(g_a);
		vec4 cubic_b = thirds_weights(g_b);
		vec4 cubic_p = thirds_weights(g_p);
		vec4 near_a = nearest_weights(g_a);
		vec4 near_b = nearest_weights(g_b);
		vec4 near_p = nearest_weights(g_p);
		float light = 0.0;
		for (int core = 0; core < 3; core++) {
			float share_core = core_share(q, core);
			if (share_core <= 0.0) {
				continue;
			}
			int cell = p.cell + (q.rmedia > 1 ? core : 0) * q.rsamples * 4;
			// the boundary ray before this one (0), and the one before that (00)
			float xa0 = 0.0;
			float xb0 = 0.0;
			float pass0 = 0.0;
			float chord0 = 0.0;
			float route0 = 0.0;
			float m0 = 0.0;
			WallAt at_a0;
			WallAt at_b0;
			float pass00 = 0.0;
			float chord00 = 0.0;
			float route00 = 0.0;
			WallAt origin;
			float pend = 0.0; // the far side of a route's change, waiting for the ray beyond it: its share of a part
			float sum = 0.0;
			for (int k = 0; k <= 5; k++) {
				if (k > p.parts && (pend <= 0.0 || k > p.parts + 1 || p.first + k * (p.stride / 4) > q.rsamples - 1)) {
					break;
				}
				// the boundary ray: the cubic through its four nodes, or (its route changing with the wavelength) its nearest node
				int at = cell + k * p.stride;
				vec4 land = map_vec(at);
				vec4 routes = map_vec(at + 3);
				bool whole = floor(routes.y) == floor(routes.x) && floor(routes.z) == floor(routes.x) && floor(routes.w) == floor(routes.x);
				vec4 weigh_p = whole ? cubic_p : near_p;
				float xa = q.raxis + dot(whole ? cubic_a : near_a, land);
				float xb = q.raxis + dot(whole ? cubic_b : near_b, land);
				float pass = clamp(dot(weigh_p, map_vec(at + 1)), 0.0, 1.0);
				float chord = sqrt(max(dot(weigh_p, map_vec(at + 2)), 0.0));
				float route = dot(near_p, routes); // with half of how far along the route changes before the next sample
				WallAt at_a = wall_at(p.profile, xa);
				WallAt at_b = wall_move(p.profile, at_a, xb);
				if (k == 0) {
					origin = at_a;
				}
				float m = wall_mean(p.profile, origin, at_a, xa, at_b, xb);
				bool same = floor(route) == floor(route0);
				if (pend > 0.0) {
					// the far side of the change in the part before: the last ray's, carried back as this one says
					sum = sum + pend * side_light(q, shade, core == 0, at_a0, xa0, at_b0, xb0, pass0, chord0, pass, chord, same, pend);
					pend = 0.0;
				}
				if (k > 0 && k <= p.parts) {
					if (!same) {
						// The route changes in the part (a rim): each side is its own ray's, by the share the map gives
						// it. The near side now, carried on as the ray before says; the far side when its ray beyond is read.
						float left = p.stride == 4 ? 2.0 * fract(route0) : 0.5;
						sum = sum + left * side_light(q, shade, core == 0, at_a0, xa0, at_b0, xb0, pass0, chord0, pass00, chord00, k > 1 && floor(route00) == floor(route0), left);
						pend = 1.0 - left;
					} else {
						// the wall over the part's sweep; a part without width, or folded over on the way, is its two rays'
						float width = 0.5 * ((xa - xa0) + (xb - xb0));
						float wall = abs(width) > 1.0e-4 && (xa - xa0) * (xb - xb0) > 0.0 ? (m - m0) / width : 0.5 * (wall_along(at_a0, xa0, at_b0, xb0) + wall_along(at_a, xa, at_b, xb));
						sum = sum + shaded(shade, q, 0.5 * (pass0 + pass), chord0, chord, core == 0, wall);
					}
				}
				pass00 = pass0;
				chord00 = chord0;
				route00 = route0;
				xa0 = xa;
				xb0 = xb;
				pass0 = pass;
				chord0 = chord;
				route0 = route;
				m0 = m;
				at_a0 = at_a;
				at_b0 = at_b;
			}
			if (pend > 0.0) {
				// at the map's own end: the last ray alone
				sum = sum + pend * shaded(shade, q, pass0, chord0, chord0, core == 0, wall_along(at_a0, xa0, at_b0, xb0));
			}
			light = light + share_core * sum / float(p.parts);
		}
		if (q.rin < 1.0) {
			light = light + (1.0 - q.rin) * shaded(shade, q, 1.0, 0.0, 0.0, false, p.straight);
		}
		total = total + weight * light;
		weights = weights + weight;
		begin = end;
		u_begin = u_end;
	}
	return weights > 0.0 ? total / weights : 0.0;
}

// The share of the room's light that reaches the eye over a band of its energy, as a picture reads it:
// through a map of one piece by the reading made for it, else by the general one.
// whether a pixel is read by the chain: a vessel bends its view, and the reading for one vessel is not its
bool chained(Path q) {
	return q.refractor >= 0 && !plain_of(q).ok;
}

float band_light(Path q, float u0, float u1, float t, float one_nm) {
	Plain plain = plain_of(q);
	if (plain.ok) {
		return plain_band(q, plain, u0, u1, t, one_nm);
	}
	return other_band(q, u0, u1, t, one_nm);
}

// ---- the light in bands of equal energy

vec4 share(float u) {
	float x = clamp(u, 0.0, 1.0) * 4096.0;
	int j = min(int(floor(x)), 4095);
	return mix(light_vec(j), light_vec(j + 1), x - float(j));
}

vec3 to_linear_srgb(vec3 c) {
	return vec3(3.2404542 * c.x - 1.5371385 * c.y - 0.4985314 * c.z, -0.9692660 * c.x + 1.8760108 * c.y + 0.0415560 * c.z, 0.0556434 * c.x - 0.2040259 * c.y + 1.0572252 * c.z);
}

// Where a pixel's light spikes in wavelength within a band (GAME_SPEC §12.2): the shares of the light's
// energy at which it does, ascending. A band cut there is read on each side of the spike, by the moving
// points and by the fixed estimate alike. Only a narrow absorption line in what the pixel looks through
// needs it (its core, a width and a half to each side): a bent view sweeping across the wall's edges is
// integrated exactly (core_light), and a light's own narrow lines have their shares in its energy table.
const int MAX_CUTS = 24;
struct Cuts {
	float u[MAX_CUTS];
	int count;
};

// the share of the light's energy at a wavelength (the table is ascending in both)
float share_at(float nm) {
	float a = 0.0;
	float b = 1.0;
	for (int it = 0; it < 14; it++) {
		float m = 0.5 * (a + b);
		if (share(m).x + 380.0 < nm) {
			a = m;
		} else {
			b = m;
		}
	}
	return 0.5 * (a + b);
}

Cuts add_cut(Cuts c, float u) {
	if (c.count < MAX_CUTS && u > 0.0 && u < 1.0) {
		c.u[c.count] = u;
		c.count = c.count + 1;
	}
	return c;
}

// the lines of one layer's liquid
Cuts line_cuts(Cuts c, Layer l, int layers, int k) {
	if (k >= layers || l.depth_cm <= 0.0) {
		return c;
	}
	vec4 ids = scene_vec(HEADER_VECS + l.body * BODY_VECS + 4);
	int first = int(ids.z);
	int count = int(ids.w);
	for (int j = 0; j < 256; j++) {
		if (j >= count || c.count + 2 > MAX_CUTS) {
			break;
		}
		vec4 ln = line_vec(first + j);
		c = add_cut(c, share_at(ln.x - 1.5 * ln.y));
		c = add_cut(c, share_at(ln.x + 1.5 * ln.y));
	}
	return c;
}

Cuts cuts_of(Path q) {
	Cuts c;
	c.count = 0;
	for (int i = 0; i < MAX_CUTS; i++) {
		c.u[i] = 1.0;
	}
	if (scene_vec(2).w < 0.5) {
		return c;
	}
	c = line_cuts(c, q.l0, q.layers, 0);
	c = line_cuts(c, q.l1, q.layers, 1);
	c = line_cuts(c, q.l2, q.layers, 2);
	c = line_cuts(c, q.l3, q.layers, 3);
	// ascending (insertion sort)
	for (int i = 1; i < MAX_CUTS; i++) {
		if (i >= c.count) {
			break;
		}
		float v = c.u[i];
		int j = i - 1;
		for (int t = 0; t < MAX_CUTS; t++) {
			if (j < 0 || c.u[j] <= v) {
				break;
			}
			c.u[j + 1] = c.u[j];
			j = j - 1;
		}
		c.u[j + 1] = v;
	}
	return c;
}

// n bands, each its exact colour times what gets through at one point in it; offset in [0, 1) places the points.
// A band a cut falls in is split there, each part with its own colour and its own point.
vec3 batch(Path q, int n, float offset, Cuts cuts) {
	Plain plain = plain_of(q);
	vec3 sum = vec3(0.0);
	vec4 before = share(0.0);
	float lo = 0.0;
	int j = 0;
	for (int b = 0; b < 64; b++) {
		if (b >= n) {
			break;
		}
		float band_hi = float(b + 1) / float(n);
		for (int r = 0; r <= MAX_CUTS; r++) {
			for (int t = 0; t < MAX_CUTS; t++) {
				if (j >= cuts.count || cuts.u[j] > lo) {
					break;
				}
				j = j + 1;
			}
			float hi = j < cuts.count && cuts.u[j] < band_hi ? cuts.u[j] : band_hi;
			vec4 upto = share(hi);
			sum = sum + vec3(upto.y - before.y, upto.z - before.z, upto.w - before.w) * (plain.ok ? plain_band(q, plain, lo, hi, offset, 0.0) : other_band(q, lo, hi, offset, 0.0));
			before = upto;
			lo = hi;
			if (hi >= band_hi) {
				break;
			}
		}
	}
	return to_linear_srgb(sum);
}

// the colour itself: the light's shares, each its own band, read at its middle
vec3 truth(Path q, int shares) {
	Plain plain = plain_of(q);
	vec3 sum = vec3(0.0);
	for (int j = 0; j < 4096; j++) {
		if (j >= shares) {
			break;
		}
		vec4 a = share(float(j) / float(shares));
		vec4 b = share(float(j + 1) / float(shares));
		float lo = float(j) / float(shares);
		float hi = float(j + 1) / float(shares);
		sum = sum + vec3(b.y - a.y, b.z - a.z, b.w - a.w) * (plain.ok ? plain_band(q, plain, lo, hi, 0.5, 0.0) : other_band(q, lo, hi, 0.5, 0.0));
	}
	return to_linear_srgb(sum);
}

// ---- what the screen shows

float cube_root(float v) {
	return v < 0.0 ? -pow(-v, 1.0 / 3.0) : pow(v, 1.0 / 3.0);
}

vec3 to_oklab(vec3 c) {
	float l = cube_root(0.4122214708 * c.x + 0.5363325363 * c.y + 0.0514459929 * c.z);
	float m = cube_root(0.2119034982 * c.x + 0.6806995451 * c.y + 0.1073969566 * c.z);
	float s = cube_root(0.0883024619 * c.x + 0.2817188376 * c.y + 0.6299787005 * c.z);
	return vec3(0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s, 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s, 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s);
}

vec3 from_oklab(vec3 c) {
	float l = c.x + 0.3963377774 * c.y + 0.2158037573 * c.z;
	float m = c.x - 0.1055613458 * c.y - 0.0638541728 * c.z;
	float s = c.x - 0.0894841775 * c.y - 1.2914855480 * c.z;
	l = l * l * l;
	m = m * m * m;
	s = s * s * s;
	return vec3(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s, -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s, -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s);
}

vec3 clip01(vec3 c) {
	return vec3(clamp(c.x, 0.0, 1.0), clamp(c.y, 0.0, 1.0), clamp(c.z, 0.0, 1.0));
}

// a colour no display has keeps its lightness and hue: OKLCh's chroma lowered until clipping what is
// left is within a just-noticeable difference (CSS Color 4), sixteen halvings; linear sRGB in and out
vec3 gamut(vec3 c) {
	float e = 0.000001;
	if (c.x >= -e && c.x <= 1.0 + e && c.y >= -e && c.y <= 1.0 + e && c.z >= -e && c.z <= 1.0 + e) {
		return clip01(c);
	}
	vec3 lab = to_oklab(c);
	if (lab.x >= 1.0) {
		return vec3(1.0);
	}
	if (lab.x <= 0.0) {
		return vec3(0.0);
	}
	float chroma = sqrt(lab.y * lab.y + lab.z * lab.z);
	float lo = 0.0;
	float hi = chroma;
	for (int i = 0; i < 16; i++) {
		float mid = (lo + hi) * 0.5;
		float scale = chroma > 0.0 ? mid / chroma : 0.0;
		vec3 at = vec3(lab.x, lab.y * scale, lab.z * scale);
		vec3 back = to_oklab(clip01(from_oklab(at)));
		float d = sqrt((back.x - at.x) * (back.x - at.x) + (back.y - at.y) * (back.y - at.y) + (back.z - at.z) * (back.z - at.z));
		if (d < 0.02) {
			lo = mid;
		} else {
			hi = mid;
		}
	}
	float scale = chroma > 0.0 ? lo / chroma : 0.0;
	return clip01(from_oklab(vec3(lab.x, lab.y * scale, lab.z * scale)));
}

float encode1(float v) {
	return v <= 0.0031308 ? 12.92 * v : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}

// CIE L*a*b* of what the screen shows of a colour (gamut-mapped; D65's white)
float lab_f(float t) {
	return t > 0.008856451679 ? cube_root(t) : (903.2962963 * t + 16.0) / 116.0;
}
vec3 lab_of(vec3 rgb) {
	vec3 c = gamut(rgb);
	float x = (0.4124564 * c.x + 0.3575761 * c.y + 0.1804375 * c.z) / 0.95047;
	float y = 0.2126729 * c.x + 0.7151522 * c.y + 0.0721750 * c.z;
	float z = (0.0193339 * c.x + 0.1191920 * c.y + 0.9503041 * c.z) / 1.08883;
	float fx = lab_f(x);
	float fy = lab_f(y);
	float fz = lab_f(z);
	return vec3(116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz));
}

float hue_degrees(float b, float a) {
	if (a == 0.0 && b == 0.0) {
		return 0.0;
	}
	float h = atan(b, a) * 57.29577951308232;
	return h < 0.0 ? h + 360.0 : h;
}

// the eye metric (GAME_SPEC §12.2), fitted to the user's marks: CIEDE2000 with lightness weighed double
// (kL 0.5), chroma a third (kC 3), hue a half (kH 2), and a grey shown as a colour counted by chroma's ratio
float eye(vec3 p, vec3 q) {
	float rad = 0.017453292519943295;
	float c1 = sqrt(p.y * p.y + p.z * p.z);
	float c2 = sqrt(q.y * q.y + q.z * q.z);
	float cb7 = pow((c1 + c2) * 0.5, 7.0);
	float g = 0.5 * (1.0 - sqrt(cb7 / (cb7 + 6103515625.0)));
	float a1 = (1.0 + g) * p.y;
	float a2 = (1.0 + g) * q.y;
	float c1p = sqrt(a1 * a1 + p.z * p.z);
	float c2p = sqrt(a2 * a2 + q.z * q.z);
	float h1 = hue_degrees(p.z, a1);
	float h2 = hue_degrees(q.z, a2);
	float dl = q.x - p.x;
	float dc = c2p - c1p;
	float dh = 0.0;
	if (c1p * c2p != 0.0) {
		dh = h2 - h1;
		dh = dh > 180.0 ? dh - 360.0 : (dh < -180.0 ? dh + 360.0 : dh);
	}
	float big_dh = 2.0 * sqrt(c1p * c2p) * sin(dh * 0.5 * rad);
	float lb = (p.x + q.x) * 0.5;
	float cb = (c1p + c2p) * 0.5;
	float hb = h1 + h2;
	if (c1p * c2p != 0.0) {
		hb = abs(h1 - h2) <= 180.0 ? (h1 + h2) * 0.5 : (h1 + h2 < 360.0 ? (h1 + h2 + 360.0) * 0.5 : (h1 + h2 - 360.0) * 0.5);
	}
	float t = 1.0 - 0.17 * cos((hb - 30.0) * rad) + 0.24 * cos(2.0 * hb * rad) + 0.32 * cos((3.0 * hb + 6.0) * rad) - 0.20 * cos((4.0 * hb - 63.0) * rad);
	float u = (hb - 275.0) / 25.0;
	float dth = 30.0 * exp(-u * u);
	float cb7b = pow(cb, 7.0);
	float rc = 2.0 * sqrt(cb7b / (cb7b + 6103515625.0));
	float sl = 1.0 + 0.015 * (lb - 50.0) * (lb - 50.0) / sqrt(20.0 + (lb - 50.0) * (lb - 50.0));
	float sc = 1.0 + 0.045 * cb;
	float sh = 1.0 + 0.015 * cb * t;
	float rt = -sin(2.0 * dth * rad) * rc;
	float vl = dl / (0.5 * sl);
	float vc = dc / (3.0 * sc);
	float vh = big_dh / (2.0 * sh);
	float base = sqrt(max(vl * vl + vc * vc + vh * vh + rt * vc * vh, 0.0));
	float neutral = 8.0 * abs(log((c2 + 8.0) / (c1 + 8.0)));
	return sqrt(base * base + neutral * neutral);
}

// ---- one frame of the method

// a number in [0, 1) for a pixel and a frame, the same on any machine
float hash01(int x, int y, int frame) {
	uint h = uint(x) * 1973u + uint(y) * 9277u + uint(frame) * 26699u + 911u;
	h = (h ^ (h >> 15u)) * 739982445u;
	h = (h ^ (h >> 12u)) * 695872825u;
	h = h ^ (h >> 15u);
	return float(h >> 8u) / 16777216.0;
}

// the budget: asks above `budget` points a pixel over `pixels` are scaled down, by last frame's total ask
float budget_scale(float asked, float budget, float pixels) {
	return asked > budget * pixels ? budget * pixels / asked : 1.0;
}

// a pixel whose content is the same and which asked for nothing last frame has nothing to do: its state
// and what it shows stay as they are (the GPU then neither reads its history nor writes anything)
bool done(Path q, vec4 key, vec4 aux) {
	return key.x == q.key.x && key.y == q.key.y && key.z == q.key.z && key.w == q.key.w && aux.z == 0.0;
}

// p is the pixel's centre; scale from budget_scale
PixelState step_pixel(Path q, PixelState s, vec2 p, int frame, float scale) {
	vec3 d = vec3(s.hist.x, s.hist.y, s.hist.z);
	float points = s.hist.w;
	vec3 prox = vec3(s.prox.x, s.prox.y, s.prox.z);
	float drift = s.prox.w;
	float noise = s.aux.x;
	float frames = s.aux.y;
	float spent = 0.0;
	// the content changed: the fixed estimate again, and whether the history still holds
	Cuts cuts = cuts_of(q);
	if (s.key.x != q.key.x || s.key.y != q.key.y || s.key.z != q.key.z || s.key.w != q.key.w) {
		vec3 now = batch(q, PROXY_POINTS, 0.5, cuts);
		spent = spent + float(PROXY_POINTS + cuts.count);
		bool keep = false;
		if (s.key.z == q.key.z && points > 0.0) {
			float moved = eye(lab_of(prox), lab_of(now));
			drift = drift + moved;
			keep = drift <= DRIFT_MOST;
			if (keep) {
				points = points * pow(0.5, moved);
				frames = min(frames, SETTLED_FRAMES);
			}
		}
		if (!keep) {
			d = vec3(0.0);
			points = 0.0;
			drift = 0.0;
			noise = 0.0;
			frames = 0.0;
		}
		prox = now;
	}
	// points by need
	float want = 0.0;
	if (points < 1.0 || frames < SETTLED_FRAMES) {
		// a pixel through a stack starts with the least batch: its points cost several of one vessel's, and
		// while what is held moves its history starts again every frame; once it is still it has the rest
		want = max((q.chain ? float(LEAST_BATCH) : FRESH_POINTS) - points, 2.0);
	} else {
		want = max(max(noise / (TARGET * TARGET) - points, SETTLED_POINTS - points), 0.0);
	}
	want = min(want, MOST_POINTS);
	// a batch is never under LEAST_BATCH points, so what is asked is what it costs: a whole batch, or (for
	// less than a point) the chance of one; under a budget, a batch is drawn with the chance that keeps the
	// points spent to the points the budget allows
	float ask = want < 1.0 ? want * float(LEAST_BATCH) : max(want, float(LEAST_BATCH));
	float scaled = ask * scale;
	float chance = hash01(int(p.x), int(p.y), frame);
	int n = scaled >= float(LEAST_BATCH) ? int(floor(scaled + chance)) : (chance < scaled / float(LEAST_BATCH) ? LEAST_BATCH : 0);
	want = ask;
	if (n > 0) {
		float seed = fract(0.7548776662 * p.x + 0.56984029 * p.y);
		vec3 est = batch(q, n, fract(seed + 0.6180339887 * float(frame % 8192)), cuts);
		spent = spent + float(n + cuts.count);
		if (points >= 1.0) {
			float dev = eye(lab_of(prox + d), lab_of(est));
			float trial = dev * dev / (1.0 / float(n) + 1.0 / points);
			noise = frames >= 1.0 ? noise + (trial - noise) / min(frames + 1.0, 16.0) : trial;
		}
		d = (d * points + (est - prox) * float(n)) / (points + float(n));
		points = min(points + float(n), 4096.0);
		frames = min(frames + 1.0, 256.0);
	}
	PixelState next;
	next.hist = vec4(d.x, d.y, d.z, points);
	next.key = q.key;
	next.prox = vec4(prox.x, prox.y, prox.z, drift);
	next.aux = vec4(noise, frames, want, spent);
	return next;
}

// what the pixel shows: the fixed estimate and what its history adds; sRGB-encoded, alpha 0 where no body is
vec4 shown(Path q, PixelState s) {
	vec3 c = gamut(vec3(s.prox.x + s.hist.x, s.prox.y + s.hist.y, s.prox.z + s.hist.z));
	return vec4(encode1(c.x), encode1(c.y), encode1(c.z), q.cover > 0.0 ? 1.0 : 0.0);
}
