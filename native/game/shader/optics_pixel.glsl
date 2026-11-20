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

const int HEADER_VECS = 4;
const int BODY_VECS = 7;
const int ROW = 401;
const int ROWS_PER_BODY = 5;
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
	vec3 glow; // what hot solids emit, linear sRGB
	vec4 key; // everything the pixel's colour depends on that can change
	// where it looks from, and the vessel that bends its view (GAME_SPEC §12.3), if any
	float x;
	float y;
	int refractor; // that body, or -1
	float raxis; // its axis on the screen (pixels)
	int rmap; // its map: where it starts among the maps, the whole pixels it spans, its samples, pieces in wavelength and cores
	int rcoarse;
	int rsamples;
	int rpieces;
	int rmedia;
	float rleft; // the map's first sample, pixels from the axis
	float rwet; // how much of the pixel's height is under its liquid's surface
	float rin; // how much of the pixel's height is between its top and its floor
};

// a pixel's state between frames
struct PixelState {
	vec4 hist; // what the moving points add to the fixed estimate (linear sRGB, averaged), and the points behind it
	vec4 key; // the content key it belongs to
	vec4 prox; // the fixed estimate (linear sRGB), and how far it has moved since the history began
	vec4 aux; // the noise of one point (eye metric, squared), frames in the history, points asked for, points spent
};

// ---- where the pixel looks

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
	q.refractor = -1;
	q.raxis = 0.0;
	q.rmap = 0;
	q.rcoarse = 0;
	q.rsamples = 0;
	q.rpieces = 0;
	q.rmedia = 0;
	q.rleft = 0.0;
	q.rwet = 0.0;
	q.rin = 0.0;
	vec4 head = scene_vec(0);
	vec4 bench = scene_vec(1);
	bool on_bench = p.x >= bench.x && p.x < bench.x + bench.z && p.y >= bench.y && p.y < bench.y + bench.w;
	q.back = on_bench ? head.y : head.z;
	int count = int(head.x);
	for (int i = 0; i < 16; i++) {
		if (i >= count) {
			break;
		}
		int at = HEADER_VECS + i * BODY_VECS;
		vec4 rect = scene_vec(at);
		if (p.x < rect.x - 1.0 || p.x > rect.x + rect.z + 1.0 || p.y < rect.y - 1.0 || p.y > rect.y + rect.w + 1.0) {
			continue;
		}
		vec4 form = scene_vec(at + 1);
		vec4 more = scene_vec(at + 2);
		float radius = rect.z * 0.5;
		float from_axis = p.x - (rect.x + radius);
		float dx = from_axis / radius;
		float ground = rect.y + rect.w; // where its solids rest, under this pixel
		float within = 0.0; // pixels inside its outline
		float chord = 0.0; // the path through it, in diameters
		float wall = 0.0; // the wall crossed, in reference thicknesses
		if (form.x > 1.5) {
			// a prism standing upright: seen from the side its silhouette is its rectangle
			within = min(min(p.x - rect.x, rect.x + rect.z - p.x), min(p.y - rect.y, ground - p.y));
		} else if (form.x < 0.5) {
			// a cylinder seen from the side
			within = min(radius * (1.0 - abs(dx)), min(p.y - rect.y, ground - p.y));
			chord = sqrt(max(1.0 - dx * dx, 0.0));
		} else {
			// a ball: its skin is crossed twice, and aslant near the rim
			float half_height = rect.w * 0.5;
			float dy = (p.y - (rect.y + half_height)) / half_height;
			float d = sqrt(dx * dx + dy * dy);
			within = min(radius, half_height) * (1.0 - d);
			chord = sqrt(max(1.0 - d * d, 0.0));
			wall = min(2.0 * more.y / max(chord, 0.001), WALL_MOST);
			ground = rect.y + half_height + half_height * sqrt(max(1.0 - dx * dx, 0.0));
		}
		float cover = clamp(within / size + 0.5, 0.0, 1.0);
		if (cover <= 0.0 || q.layers >= MAX_LAYERS) {
			continue;
		}
		float cm = 2.0 * radius * chord * form.y;
		// a pixel half under the surface is half as deep in it: T^coverage
		float wet = clamp((p.y - form.z) / size + 0.5, 0.0, 1.0) * cover;
		float heap = form.x > 1.5 ? 0.0 : form.w * max(1.0 - abs(from_axis) / max(more.z, 0.001), 0.0);
		Layer l;
		l.body = i;
		l.depth_cm = cm * wet;
		l.haze = form.x > 1.5 ? 0.0 : 1.0 - exp(-more.x * cm * wet);
		l.solid = form.w > 0.0 ? clamp((p.y - (ground - heap)) / size + 0.5, 0.0, 1.0) * cover : 0.0;
		l.wall = wall * cover;
		l.wet = wet;
		vec4 bend = scene_vec(at + 5);
		if (bend.w > 0.5 && q.refractor < 0) {
			// a vessel bends the view: the first one the pixel looks through
			q.refractor = i;
			q.raxis = rect.x + radius;
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
		q.cover = max(q.cover, cover);
		vec4 ids = scene_vec(at + 4);
		float w = 1.0 + 0.6180339887 * float(i);
		q.key = q.key + vec4((l.depth_cm + 3.0 * l.haze + 0.29 * cover) * w, (l.solid + 7.0 * l.wall) * w, ids.x, ids.y * w);
	}
	// what glows is shown as it is, outside the history; it is in the key so that a pixel shows it anew
	q.key.y = q.key.y + 13.0 * q.back + 0.001 * (q.glow.x + 2.0 * q.glow.y + 3.0 * q.glow.z);
	// where the view goes through a refracting body: where across it, and how much of the pixel is under its liquid and within it
	if (q.refractor >= 0) {
		q.key.x = q.key.x + 0.37 * (q.x - q.raxis) + 0.19 * q.rwet + 0.43 * q.rin;
		q.key.y = q.key.y + 0.31 * q.raxis;
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
	// what lies in it is lit and seen through it: there and back is one chord
	seen = mix(seen, colour * exp(-depth), l.solid);
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

// what a ray does: where it lands on the wall (x, on the screen), what the body's surfaces let through
// (pass, Fresnel), the chord of its liquid it crosses (cm), and its route (the same for rays that go the same way)
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

// A ray of a body at every wavelength of one piece of its map: at the piece's four nodes (its ends and its
// thirds) where it lands (pixels from the body's axis), what the surfaces pass, its chord squared, its route.
struct Edge {
	vec4 land;
	vec4 pass;
	vec4 chord;
	vec4 route;
};

// The ray at x (pixels from the body's axis), through its core as liquid (medium 0 of two) or as air, in
// one piece of the map: straight between two samples across (the nearer one's where the route changes
// between them); on a sample (a pixel's parts begin and end on samples, wherever a body stands on whole or
// half pixels), that sample's cell alone. Past the map's ends a ray misses the body and goes straight.
Edge edge_load(Path q, int medium, float x, int piece) {
	Edge e;
	e.land = vec4(x, x, x, x);
	e.pass = vec4(1.0, 1.0, 1.0, 1.0);
	e.chord = vec4(0.0, 0.0, 0.0, 0.0);
	e.route = vec4(-1.0, -1.0, -1.0, -1.0);
	float s = (x - q.rleft) * MAP_FINE;
	if (q.rsamples < 2 || s < 0.0 || s > float(q.rsamples - 1)) {
		return e;
	}
	int i = min(int(floor(s)), q.rsamples - 2);
	float f = s - float(i);
	int base = q.rmap + 1 + q.rcoarse + ((medium * q.rsamples + i) * q.rpieces + piece) * 4;
	vec4 r = map_vec(base + 3);
	if (f > 0.0) {
		int next = base + q.rpieces * 4;
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
			e.land = mix(map_vec(base), map_vec(next), f);
			e.pass = mix(map_vec(base + 1), map_vec(next + 1), f);
			e.chord = mix(map_vec(base + 2), map_vec(next + 2), f);
			e.route = vec4(floor(r.x), floor(r.y), floor(r.z), floor(r.w));
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

// one of four values: the node nearest to g of the way through the piece
float nearest(vec4 v, float g) {
	return g < 0.5 ? (g < 0.1666667 ? v.x : v.y) : (g < 0.8333333 ? v.z : v.w);
}

// the ray at g of the way through its piece: the cubic through its four nodes; or, if its route changes in
// the piece, its nearest node's ray
Bend edge_at(Path q, Edge e, float g) {
	Bend b;
	if (e.route.y == e.route.x && e.route.z == e.route.x && e.route.w == e.route.x) {
		b.x = q.raxis + thirds(e.land, g);
		b.pass = clamp(thirds(e.pass, g), 0.0, 1.0);
		b.chord = sqrt(max(thirds(e.chord, g), 0.0)); // the map holds its square
		b.route = e.route.x;
	} else {
		b.x = q.raxis + nearest(e.land, g);
		b.pass = nearest(e.pass, g);
		b.chord = sqrt(max(nearest(e.chord, g), 0.0));
		b.route = nearest(e.route, g);
	}
	return b;
}

// the ray at x and nm
Bend map_at(Path q, int medium, float x, float nm) {
	float place = map_place(nm) * float(q.rpieces);
	int piece = min(int(floor(place)), q.rpieces - 1);
	return edge_at(q, edge_load(q, medium, x, piece), place - float(piece));
}

// into how many equal parts the whole pixel of the body that x is in must be read
float map_level(Path q, float x) {
	int c = int(floor(x - q.rleft));
	if (c < 0 || c >= q.rcoarse) {
		return 1.0;
	}
	return map_vec(q.rmap + 1 + c).x;
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
	if (l.body == q.refractor && !s.own) {
		s.own = true;
		s.per_cm = per_cm;
		s.colour = colour;
		s.glass = glass;
		s.haze = l.haze;
		s.solid = l.solid;
		return s;
	}
	// through_layer, as a times what reaches it plus b
	float depth = per_cm * l.depth_cm;
	float through = exp(-depth);
	float a = through * (1.0 - l.haze) * (1.0 - l.solid) * glass;
	float b = colour * (exp(-depth * 0.5) * l.haze * (1.0 - l.solid) + through * l.solid) * glass;
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
// surfaces turn away showing the room in front
float shaded(Shade s, Path q, float pass, float c0, float c1, bool liquid, float wall) {
	float seen = s.a_behind * wall + s.b_behind;
	if (s.own) {
		float depth = liquid ? s.per_cm * chord_of_part(c0, c1, s.per_cm) : 0.0;
		float through = exp(-depth);
		seen = seen * through;
		seen = mix(seen, s.colour * exp(-depth * 0.5), s.haze);
		seen = mix(seen, s.colour * through, s.solid);
		seen = seen * s.glass;
	}
	seen = s.a_front * seen + s.b_front;
	if (q.refractor >= 0) {
		seen = pass * seen + (1.0 - pass) * s.room;
	}
	return seen;
}

float along(Path q, float nm, float pass, float c0, float c1, bool liquid, float wall) {
	return shaded(shade_of(q, nm), q, pass, c0, c1, liquid, wall);
}

// how fast the landing bends in wavelength, at most, in the whole pixel of the body that x is in (pixels of
// the screen per nm^2)
float map_bend(Path q, float x) {
	int c = int(floor(x - q.rleft));
	if (c < 0 || c >= q.rcoarse) {
		return 0.0;
	}
	return map_vec(q.rmap + 1 + c).y;
}

// in how many straight steps a band of `nm` nanometres is swept, where the landing bends so: as many as keep
// the sweep within MAP_SWEEP_TOLERANCE (0.02 pixels of the screen) of the true landing; at most sixteen
int map_steps(float bend, float nm) {
	return int(clamp(ceil(nm * sqrt(bend / 0.16)), 1.0, 16.0));
}

// Between two places of a part whose rays go different ways (GAME_SPEC §12.3): the left ray's share of the
// part, from where the map says the route changes (in a sample's cell, how far along: twice its route's
// fraction). A rim: all of the part inside the body is the ray's that goes through.
float left_share(Path q, int medium, float left, float right, float nm) {
	float place = map_place(nm) * float(q.rpieces);
	int piece = min(int(floor(place)), q.rpieces - 1);
	float g = place - float(piece);
	int cells = q.rmap + 1 + q.rcoarse + (medium * q.rsamples * q.rpieces + piece) * 4 + 3;
	int from = int(floor((left - q.rleft) * MAP_FINE));
	for (int j = 0; j < 4; j++) {
		int i = from + j;
		if (i < 0 || i + 1 >= q.rsamples) {
			continue;
		}
		float here = nearest(map_vec(cells + i * q.rpieces * 4), g); // at the nearest node
		if (floor(here) != floor(nearest(map_vec(cells + (i + 1) * q.rpieces * 4), g))) {
			float change = q.rleft + (float(i) + 2.0 * fract(here)) / MAP_FINE;
			return clamp((change - left) / (right - left), 0.0, 1.0);
		}
		if (q.rleft + float(i + 1) / MAP_FINE >= right) {
			break;
		}
	}
	return 0.5;
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

// One part of a pixel whose rays go different ways (GAME_SPEC §12.3), between the rays at `left` and
// `right` (pixels from the body's axis), read at a step's point nm: a and b are its left ray at the step's
// two ends, a2 and b2 its right. If the point's two rays go one way, the wall at the step's two ends; if
// not, each side is its own straight piece, from its end's ray to the change of route, continued from the
// ray beyond its end if that goes the same way.
float split_part(Path q, Shade shade, int profile, int medium, bool liquid, float left, float right, float nm, Bend a, Bend a2, Bend b, Bend b2) {
	Bend lp = map_at(q, medium, left, nm);
	Bend rp = map_at(q, medium, right, nm);
	if (lp.route == rp.route) {
		float wall = 0.5 * (end_wall(profile, a, a2) + end_wall(profile, b, b2)); // a route changes on the way: the step's two ends
		return shaded(shade, q, 0.5 * (lp.pass + rp.pass), lp.chord, rp.chord, liquid, wall);
	}
	float width = right - left;
	float share_left = left_share(q, medium, left, right, nm);
	Bend l_edge = lp;
	Bend beyond = map_at(q, medium, left - width, nm);
	if (beyond.route == lp.route) {
		l_edge = toward(lp, beyond, share_left);
	}
	Bend r_edge = rp;
	beyond = map_at(q, medium, right + width, nm);
	if (beyond.route == rp.route) {
		r_edge = toward(rp, beyond, 1.0 - share_left);
	}
	return share_left * shaded(shade, q, 0.5 * (lp.pass + l_edge.pass), lp.chord, l_edge.chord, liquid, wall_span(profile, lp.x, l_edge.x)) +
		   (1.0 - share_left) * shaded(shade, q, 0.5 * (rp.pass + r_edge.pass), r_edge.chord, rp.chord, liquid, wall_span(profile, r_edge.x, rp.x));
}

// a boundary ray, whose cell `e` of the map's piece `had` is in hand, at g of the way through `piece`
Bend edge_in(Path q, int medium, float x, Edge e, int had, int piece, float g) {
	if (piece == had) {
		return edge_at(q, e, g);
	}
	return edge_at(q, edge_load(q, medium, x, piece), g);
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
		Edge e = edge_load(q, medium, x, piece_a);
		Bend a = edge_at(q, e, g_a);
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
float through_band(Path q, float u0, float u1, float t, float one_nm) {
	float nm0 = nm_at(u0, one_nm);
	float nm1 = nm_at(u1, one_nm);
	if (q.refractor < 0) {
		return along(q, nm_at(mix(u0, u1, t), one_nm), 1.0, 0.0, 0.0, false, q.back);
	}
	int profile = int(wall_vec(WALL_ROW_BASE + clamp(int(floor(q.y)), 0, WALL_ROWS - 1)).x);
	float left = q.x - 0.5 - q.raxis;
	int parts = int(max(map_level(q, left), map_level(q, left + 0.9999)));
	float across = abs(nm1 - nm0);
	int steps = map_steps(max(map_bend(q, left), map_bend(q, left + 0.9999)), across);
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
		for (int core = 0; core < 2; core++) {
			float share_core = q.rin * (core == 0 ? q.rwet : 1.0 - q.rwet);
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
	if (q.refractor < 0 || q.rpieces != 1) {
		return p;
	}
	float left = q.x - 0.5 - q.raxis;
	float s = (left - q.rleft) * MAP_FINE;
	if (s < 0.0 || abs(s - floor(s + 0.5)) > 1.0e-3 || s + MAP_FINE > float(q.rsamples) - 0.5) {
		return p;
	}
	p.profile = int(wall_vec(WALL_ROW_BASE + clamp(int(floor(q.y)), 0, WALL_ROWS - 1)).x);
	p.parts = int(map_level(q, left + 0.5));
	p.bend = map_bend(q, left + 0.5);
	p.stride = (int(MAP_FINE) / p.parts) * 4;
	p.first = int(floor(s + 0.5));
	p.cell = q.rmap + 1 + q.rcoarse + p.first * 4;
	if (q.rin < 1.0) {
		p.straight = wall_span(p.profile, q.x - 0.5, q.x + 0.5);
	}
	p.ok = true;
	return p;
}

// the weight of the node nearest to g of the way through a piece: all of it
vec4 nearest_weights(float g) {
	return g < 0.5 ? (g < 0.1666667 ? vec4(1.0, 0.0, 0.0, 0.0) : vec4(0.0, 1.0, 0.0, 0.0)) : (g < 0.8333333 ? vec4(0.0, 0.0, 1.0, 0.0) : vec4(0.0, 0.0, 0.0, 1.0));
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
		for (int core = 0; core < 2; core++) {
			float share_core = q.rin * (core == 0 ? q.rwet : 1.0 - q.rwet);
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
float band_light(Path q, float u0, float u1, float t, float one_nm) {
	Plain plain = plain_of(q);
	if (plain.ok) {
		return plain_band(q, plain, u0, u1, t, one_nm);
	}
	return through_band(q, u0, u1, t, one_nm);
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
			sum = sum + vec3(upto.y - before.y, upto.z - before.z, upto.w - before.w) * (plain.ok ? plain_band(q, plain, lo, hi, offset, 0.0) : through_band(q, lo, hi, offset, 0.0));
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
		sum = sum + vec3(b.y - a.y, b.z - a.z, b.w - a.w) * (plain.ok ? plain_band(q, plain, lo, hi, 0.5, 0.0) : through_band(q, lo, hi, 0.5, 0.0));
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
		want = max(FRESH_POINTS - points, 2.0);
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

// what the pixel shows: the fixed estimate, what its history adds, and what glows; sRGB-encoded, alpha 0 where no body is
vec4 shown(Path q, PixelState s) {
	vec3 c = gamut(vec3(s.prox.x + s.hist.x + q.glow.x, s.prox.y + s.hist.y + q.glow.y, s.prox.z + s.hist.z + q.glow.z));
	return vec4(encode1(c.x), encode1(c.y), encode1(c.z), q.cover > 0.0 ? 1.0 : 0.0);
}
