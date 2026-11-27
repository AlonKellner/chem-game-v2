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
//   vec4 wall_vec(int i)     the wall as a table (lab/optics_scene.hpp): what is drawn behind the bodies
//   vec4 shape_vec(int i)    each bending body's surfaces and how it lies (lab/optics_scene.hpp: the shapes)
//   vec4 corner_vec(int i), void corner_put(int i, vec4 v)   the lattice of pixel corners (below: corners shared)
//   vec4 kept_vec(int i), void kept_put(int i, vec4 v)       the record of the pixel being read (below: beams kept)
//   vec4 image_vec(int i)    the images things emit by (lab/optics_scene.hpp: images; Stage 42, a screen's)
//   vec4 anchor_vec(int which, int x, int y)   (OPTICS_RESAMPLE_PASS, or the reference's) a pixel's state at the
//                            view the scene was made at before the view moved: its hist (0), prox (1), aux (2), or
//                            what it showed (3, linear); or what the room showed at 1x when last seen whole (4,
//                            linear); x and y clamped to the screen (below: a view's change)

const int HEADER_VECS = 9;
const int BODY_VECS = 8;
const int MAX_BODIES = 256;
const float GRID_SQUARE = 40.0;
const int GRID_COLS = 48;
const int GRID_ROWS = 27;
const int GRID_SLOTS = 15; // a square's bodies, at most
const int GRID_VECS = 4; // a square's vec4s: its count, then its bodies
const int GRID_BASE = HEADER_VECS + MAX_BODIES * BODY_VECS;
const int ROW = 401;
const int LWIR_ROW = 301; // a row's long-wave segment (Stage 46): at 20 nm from 8000 nm, after every set's visible rows
const int ROWS_PER_SET = 10;
const int BUBBLE_ROW = 6; // what a vessel's bubbles turn back, their albedo (Stage 45)
const int BANDS_ROW = 7; // a layered vessel's bands (Stage 47)
const int LIQUID_K_ROW = 8; // its liquid's k: a metal's (Stage 78)
const int TEMPS_ROW = 9; // a vessel's temperatures by height, its wall's and its contents' (Stage 53)
const int TEMPS_CONTENTS = 49; // where its contents' begin in that row (after its wall's, at most 24 points)
const int TEMPS_HOTTEST = 399; // its hottest
const int LIGHT_ENTRIES = 4097;
const int LIGHT_SIZE = LIGHT_ENTRIES + ROW; // a light's table, then the way back (Stage 46: two lights, one a range)
const int MAX_LAYERS = 4; // bodies one pixel looks through
const int WALL_ROWS = 1080;
const int WALL_COLUMNS = 1920;
const int WALL_ROW_BASE = 2;
const int WALL_COLUMN_BASE = 1083;
const int WALL_BREAK_BASE = 3003;
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
	// which body (body_of), and its set of spectra (set_in): one number, body + 256 times its set. A pixel
	// reads its set once, in its path; a field more on each layer cost the pass 0.1 ms (a pixel's state is
	// kept in the GPU's registers, and they are few)
	int body;
	float depth_cm; // the liquid crossed, in centimetres
	float haze; // how much of the room's light its suspended solids turn back
	float solid; // how much of the pixel a heap of solid fills
	float wall; // the wall crossed, in reference thicknesses
	float glowing; // the path through glowing gas (pixels, times how much of the sample it covers): a flame
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
	float back; // the backdrop's radiance, a share of the room's white; or, where an opaque thing covers part of the pixel and no vessel bends it, the wall over the part it leaves uncovered (edge: 1)
	float edge;
	float cover; // how much of the pixel a body covers
	vec3 glow; // what hot solids emit, linear sRGB: for the key alone (the pass reads each body's glow at 1 nm)
	vec4 key; // everything the pixel's colour depends on that can change
	vec2 shape; // and of that, what its rays' ways depend on (Stage 33): its beams are kept while this is the same
	// and what lies in the bodies it looks through, apart (Stage 33): now (x) and at the frame before (y); and the
	// rows of the screen between which any of that changed since then (x to y; none: x above y). A ray that passes
	// none of those rows goes as it went.
	vec2 content;
	vec2 zone;
	// where it looks from, how wide a sample it is, and the vessel that bends its view (GAME_SPEC §12.3), if
	// any: the nearest the eye of those it looks through
	float x;
	float y;
	float size;
	int refractor; // that body, or -1
	// a grating is in its way (Stage 29): its rays go on in several ways there, and are followed
	bool ways;
	// a sensor's display it looks at (Stage 46), or -1; and which of its layers that is (those after it are in front)
	int relay;
	int relay_k;
};

// a pixel's state between frames
struct PixelState {
	vec4 hist; // what the moving points add to the fixed estimate (linear sRGB, averaged), and the points behind it
	vec4 key; // the content key it belongs to
	vec4 prox; // the fixed estimate (linear sRGB), and how far it has moved since the history began
	vec4 aux; // the noise of one point (eye metric, squared), frames in the history, points asked for, points spent
};

// The range of the spectrum a point is read in (GAME_SPEC §12.4, Stage 46): 0 the visible, 1 the long-wave infrared,
// 8-14 um. Its light, its places along the spectrum, the wall and the room in front follow it; a row is read in the
// segment its wavelength falls in.
#ifndef OPTICS_THREAD
#define OPTICS_THREAD
#endif
OPTICS_THREAD int band_range = 0;
// the bodies a ray passes by (Stage 46: bits of a body's GLOW w; a sensor's rays do not see its own body)
OPTICS_THREAD int ray_hide = 0;
OPTICS_THREAD bool range_fixed = false;
// (Stage 61) the wall's table read: the wall as drawn (0), or behind glass (the second table, from the header's vec4
// 5's w): a pixel within a body's outline reads it behind glass (path_of sets it), a bare one as drawn
OPTICS_THREAD int wall_base = 0;
vec4 wall_of(int i) {
	return wall_vec(wall_base + i);
} // (a sensor's continuation reads in the range it set, whatever the pass's)
bool hidden(int body) {
	return ray_hide != 0 && (int(scene_vec(HEADER_VECS + body * BODY_VECS + 3).w) & ray_hide) != 0;
}

// a body's temperature (K; Stage 46)
float body_kelvin(int body) {
	return scene_vec(HEADER_VECS + body * BODY_VECS + 7).y; // (its SET's y)
}

// the shares of the light's energy at which a pixel's bands are cut (GAME_SPEC §12.2: cuts_of), ascending
const int MAX_CUTS = 24;
struct Cuts {
	float u[MAX_CUTS];
	int count;
};

// ---- where the pixel looks

// the square of the screen p is in: its two vec4s, (count, b0, b1, b2) and (b3, b4, b5, b6)
// (Stage 46: the squares are the room's own, at the view: where the layout's (0, 0) is on the screen and how wide a
// square is there, the header's vec4 5; so any place of the room finds its bodies, on the screen or off it)
int square_at(vec2 p) {
	vec4 grid = scene_vec(5);
	int c = clamp(int(floor((p.x - grid.x) / grid.z)), 0, GRID_COLS - 1);
	int r = clamp(int(floor((p.y - grid.y) / grid.z)), 0, GRID_ROWS - 1);
	return GRID_BASE + GRID_VECS * (r * GRID_COLS + c);
}

// the k-th body of a square
int square_body(int square, int k) {
	vec4 v = scene_vec(square + (k + 1) / 4);
	int c = (k + 1) - 4 * ((k + 1) / 4);
	return int(c == 0 ? v.x : (c == 1 ? v.y : (c == 2 ? v.z : v.w)));
}

// the set of spectra a body is made of
int set_of(int body) {
	return int(scene_vec(HEADER_VECS + body * BODY_VECS + 7).x);
}

// a layer's body, and where its set of spectra begins among the rows
int body_of(int packed) {
	return packed - 256 * (packed / 256);
}

int set_in(int packed) {
	return (packed / 256) * ROWS_PER_SET * ROW;
}


// A body set upside down (Stage 27: a placed pour): its glass's rows from the other end. Its liquid's surface
// and what rests on its cap stay the screen's own: they lie by the room, not by the glass.
bool flipped(int body) {
	return scene_vec(HEADER_VECS + body * BODY_VECS + 7).w > 0.5;
}

// What body i is at the place p of the screen, for a sample `across` wide and `up` high (pixels): how much
// of the sample it covers (its outlines blend by that), and what the sample looks through there.
// A thing of the lab at the place p (Stage 22): how far inside its outline p is (pixels; negative outside),
// and for glowing gas the path through it there (pixels).
vec2 thing_at(vec4 form, vec4 rect, vec4 more, vec2 p) {
	if (form.x > 5.5) {
		// a grating (Stage 29): a thin plate, its rectangle; it absorbs nothing and sends each ray on in its orders
		return vec2(min(min(p.x - rect.x, rect.x + rect.z - p.x), min(p.y - rect.y, rect.y + rect.w - p.y)), 0.0);
	}
	if (form.x < 3.5) {
		// a slab: its rectangle, the corners rounded
		float r = min(form.z, min(rect.z, rect.w) * 0.5);
		float qx = abs(p.x - (rect.x + rect.z * 0.5)) - (rect.z * 0.5 - r);
		float qy = abs(p.y - (rect.y + rect.w * 0.5)) - (rect.w * 0.5 - r);
		return vec2(r - sqrt(max(qx, 0.0) * max(qx, 0.0) + max(qy, 0.0) * max(qy, 0.0)) - min(max(qx, qy), 0.0), 0.0);
	}
	if (form.x < 4.5) {
		// a bar: within half its width of its line
		float ux = more.z - more.x;
		float uy = more.w - more.y;
		float t = clamp(((p.x - more.x) * ux + (p.y - more.y) * uy) / max(ux * ux + uy * uy, 1.0e-9), 0.0, 1.0);
		float ex = p.x - (more.x + t * ux);
		float ey = p.y - (more.y + t * uy);
		float across = 0.5 * form.z - sqrt(ex * ex + ey * ey);
		if (form.w <= 0.0) {
			return vec2(across, 0.0);
		}
		// dashed (Stage 26): on for 0.6 of each dash from its start; how far inside the dash it is, along
		float along = t * sqrt(ux * ux + uy * uy);
		float phase = along - form.w * floor(along / form.w);
		float on = 0.6 * form.w;
		float inside = phase <= on ? min(phase, on - phase) : -min(phase - on, form.w - phase);
		return vec2(min(across, inside), 0.0);
	}
	// a cone, its apex at the top's centre: its round section there, seen from the side
	float from_axis = p.x - (rect.x + rect.z * 0.5);
	float reach = rect.z * 0.5 * clamp((p.y - rect.y) / max(rect.w, 1.0e-3), 0.0, 1.0);
	float inside = min(reach - abs(from_axis), min(p.y - rect.y, rect.y + rect.w - p.y));
	return vec2(inside, 2.0 * sqrt(max(reach * reach - from_axis * from_axis, 0.0)));
}

// Boiling's bubbles (GAME_SPEC §11, Stage 45). A vessel's vec4 6 holds its steam bubbles' extinction a centimetre
// at its floor, the height over its floor where they end (cm), the exponent their extinction falls by up to there,
// (1 - z / end)^exponent, and a reaction's gas's extinction a centimetre, the same at every height. Their extinction
// a centimetre at height y of the screen: 0 for any other body, under its floor, and upside down.
float bubbles_at(int body, float y) {
	int at = HEADER_VECS + body * BODY_VECS;
	vec4 b = scene_vec(at + 6);
	if (b.x <= 0.0 && b.w <= 0.0) {
		return 0.0;
	}
	vec4 rect = scene_vec(at);
	vec4 form = scene_vec(at + 1);
	if (form.x > 0.5 || flipped(body)) {
		return 0.0;
	}
	float z = (rect.y + rect.w - scene_vec(at + 5).z / form.y - y) * form.y;
	if (z < 0.0) {
		return 0.0;
	}
	float steam = b.y > 0.0 && z < b.y ? b.x * pow(1.0 - z / b.y, b.z) : 0.0;
	return steam + b.w;
}

// the share of a body's haze at height y that is bubbles, which turn the room's light back white, rather than its
// stirred-up solids, which turn it back in their colour
float white_at(int body, float y) {
	float solids = scene_vec(HEADER_VECS + body * BODY_VECS + 2).x;
	float bubbles = bubbles_at(body, y);
	return solids + bubbles > 0.0 ? bubbles / (solids + bubbles) : 0.0;
}

Layer layer_at(int i, vec2 p, float across, float up) {
	Layer l;
	l.body = i + 256 * set_of(i);
	l.depth_cm = 0.0;
	l.haze = 0.0;
	l.solid = 0.0;
	l.wall = 0.0;
	l.glowing = 0.0;
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
	if (form.x > 2.5) {
		// a thing of the lab that is no vessel (Stage 22): it bends nothing. A slab (3) and a bar (4) are
		// opaque, lit by the room's light as a heap is; a cone (5) is glowing gas, which emits along the path
		// through it and absorbs nothing. Its outline and its path are read at 4 x 4 places in the sample, each
		// blended by its own share (a thin bar's edge, and a flame's path near its edge, change within a pixel)
		float covered = 0.0;
		float glowing = 0.0;
		float fine = max(across, up) * 0.25;
		for (int j = 0; j < 4; j++) {
			for (int k = 0; k < 4; k++) {
				vec2 at_k = vec2(p.x + (float(k) - 1.5) * 0.25 * across, p.y + (float(j) - 1.5) * 0.25 * up);
				vec2 seen = thing_at(form, rect, more, at_k);
				float share_k = clamp(seen.x / fine + 0.5, 0.0, 1.0);
				covered = covered + share_k;
				glowing = glowing + seen.y * share_k;
			}
		}
		l.cover = covered / 16.0;
		l.solid = form.x < 4.5 ? l.cover : 0.0;
		l.glowing = glowing / 16.0;
		return l;
	}
	if (form.x > 1.5) {
		// a prism standing upright: seen from the side its silhouette is its rectangle
		within = min(min(p.x - rect.x, rect.x + rect.z - p.x) / across, min(p.y - rect.y, ground - p.y) / up);
	} else if (form.x < 0.5) {
		// a cylinder seen from the side
		within = min(radius * (1.0 - abs(dx)) / across, min(p.y - rect.y, ground - p.y) / up);
		chord = sqrt(max(1.0 - dx * dx, 0.0));
		// what it holds rests on its floor, which is glass all through
		side = clamp(radius * (1.0 - abs(dx)) / across + 0.5, 0.0, 1.0);
		float floor_px = scene_vec(at + 5).z / form.y;
		if (flipped(i)) {
			// upside down: its floor is at the top; what it holds lies on its cap, at the bottom
			under = clamp((rect.y + floor_px - p.y) / up + 0.5, 0.0, 1.0);
		} else {
			ground = ground - floor_px;
			under = clamp((p.y - ground) / up + 0.5, 0.0, 1.0);
		}
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
	l.haze = form.x > 1.5 ? 0.0 : 1.0 - exp(-(more.x + bubbles_at(i, p.y)) * cm * wet);
	l.solid = form.w <= 0.0 ? 0.0 : (form.x < 0.5 ? max(clamp((p.y - (ground - heap)) / up + 0.5, 0.0, 1.0) - under, 0.0) * side : clamp((p.y - (ground - heap)) / up + 0.5, 0.0, 1.0) * cover);
	l.wall = wall * cover;
	l.cover = cover;
	return l;
}

float wall_span(int profile, float a, float b);
int wall_profile(float y);

// whether a layer is an opaque thing of the lab (a slab or a bar) that covers part of its sample, not all
bool opaque_edge_of(Layer l) {
	float kind = scene_vec(HEADER_VECS + body_of(l.body) * BODY_VECS + 1).x;
	return kind > 2.5 && kind < 4.5 && l.solid > 0.0 && l.solid < 1.0;
}

bool opaque_edge(Path q) {
	return (q.layers > 0 && opaque_edge_of(q.l0)) || (q.layers > 1 && opaque_edge_of(q.l1)) || (q.layers > 2 && opaque_edge_of(q.l2)) || (q.layers > 3 && opaque_edge_of(q.l3));
}

// how much of the c-th quarter of the sample, across, a layer leaves uncovered (an opaque thing's; others: all)
float column_open(Layer l, Path q, int c) {
	int at = HEADER_VECS + body_of(l.body) * BODY_VECS;
	vec4 form = scene_vec(at + 1);
	if (form.x < 2.5 || form.x > 4.5) {
		return 1.0;
	}
	vec4 rect = scene_vec(at);
	vec4 more = scene_vec(at + 2);
	float fine = q.size * 0.25;
	float covered = 0.0;
	for (int j = 0; j < 4; j++) {
		vec2 seen = thing_at(form, rect, more, vec2(q.x + (float(c) - 1.5) * fine, q.y + (float(j) - 1.5) * fine));
		covered = covered + clamp(seen.x / fine + 0.5, 0.0, 1.0);
	}
	return 1.0 - 0.25 * covered;
}

float uncovered(Path q, int c) {
	float u = 1.0;
	if (q.layers > 0) {
		u = u * column_open(q.l0, q, c);
	}
	if (q.layers > 1) {
		u = u * column_open(q.l1, q, c);
	}
	if (q.layers > 2) {
		u = u * column_open(q.l2, q, c);
	}
	if (q.layers > 3) {
		u = u * column_open(q.l3, q, c);
	}
	return u;
}

// Whether a sample of the screen `size` across at p lies wholly beyond body i's outline (Stage 33). A shape
// turned about its axis is seen from the side as its profile, however it is turned on the screen: the scene
// gives how far from its axis it reaches in each band of its height (lab/optics_scene.hpp: OUTLINE_BANDS, after
// the shape's patches). No ray of such a pixel meets the body, so the pixel is not the body's: it follows none.
bool beyond_outline(int i, vec2 p, float size) {
	int block = i * 10; // (10: SHAPE_BLOCK)
	vec4 head = shape_vec(block);
	int s = int(head.x);
	if (s < 0) {
		return false;
	}
	vec4 shape = shape_vec(s);
	if (!(shape.w > shape.z)) {
		return false; // no outline: a prism, a plain cylinder (its rectangle is its outline)
	}
	vec4 form = scene_vec(HEADER_VECS + i * BODY_VECS + 1);
	vec4 turn = shape_vec(block + 1);
	vec4 rect = shape_vec(block + 2); // where it stands upright
	bool turned = head.w != 0.0;
	float c = turned ? turn.z : 1.0;
	float sn = turned ? turn.w : 0.0;
	vec2 pivot = turned ? vec2(turn.x, turn.y) : vec2(0.0, 0.0);
	vec2 back = vec2(pivot.x + (p.x - pivot.x) * c + (p.y - pivot.y) * sn, pivot.y - (p.x - pivot.x) * sn + (p.y - pivot.y) * c);
	float cm = form.y;
	float from_axis = abs(back.x - (rect.x + rect.z * 0.5)) * cm;
	float height = (rect.y + rect.w - back.y) * cm;
	float margin = (0.75 * size + 0.5) * cm; // the sample's half diagonal, and half a pixel
	float bands = 32.0 / (shape.w - shape.z);
	float lo = (height - margin - shape.z) * bands;
	float hi = (height + margin - shape.z) * bands;
	if (hi < 0.0 || lo >= 32.0) {
		return true; // above it, or under it
	}
	int first = int(clamp(floor(lo), 0.0, 31.0));
	int last = int(clamp(floor(hi), 0.0, 31.0));
	if (last - first > 3) {
		return false; // (a sample as tall as the body: not asked)
	}
	int at = (s + 1 + int(shape.x) * 6) * 4; // (6: SHAPE_PATCH) the outline's bands, a float each
	float most = 0.0;
	for (int k = first; k <= last; k++) {
		most = max(most, shape_vec((at + k) / 4)[(at + k) % 4]);
	}
	return from_axis - margin > most;
}

// what the pixel at p looks through; `size` is how wide a sample it is, in pixels: 1 for a pixel (its
// outlines blend by how much of it they cover), less for a sample of the truth (GAME_SPEC §12.3)
// (Stage 61) A body of the screen's panels (Supplies' pictures, what stands in Storage or the trash): the panels are
// the screen's, not things of the room, so what is in hand over them sees the room's wall through it, not them. Its
// bit among the hidden ones (cameras use the 23 below it).
const int PANEL_BIT = 8388608;
bool in_panel(int body) {
	return (int(scene_vec(HEADER_VECS + body * BODY_VECS + 3).w) & PANEL_BIT) != 0;
}
// whether something in hand (a layer nearer the eye) covers p, within its outline
bool hand_over(vec2 p) {
	int square = square_at(p);
	int listed = int(scene_vec(square).x);
	for (int k = 0; k < GRID_SLOTS; k++) {
		if (k >= listed) {
			break;
		}
		int i = square_body(square, k);
		if (scene_vec(HEADER_VECS + i * BODY_VECS + 5).y < 0.5) {
			continue;
		}
		vec4 rect = scene_vec(HEADER_VECS + i * BODY_VECS);
		if (p.x >= rect.x && p.x <= rect.x + rect.z && p.y >= rect.y && p.y <= rect.y + rect.w && !beyond_outline(i, p, 1.0)) {
			return true;
		}
	}
	return false;
}
// hidden from this ray (a sensor's), or a panel's under what is in hand at p
bool hidden_at(int body, vec2 p) {
	return hidden(body) || (in_panel(body) && hand_over(p));
}

Path path_of(vec2 p, float size) {
	wall_base = 0;
	Path q;
	if (!range_fixed) {
		band_range = scene_vec(4).w > 0.5 ? 1 : 0; // the range the whole pass is read in (Stage 46: the debug infrared view)
	}
	q.relay = -1;
	q.relay_k = -1;
	q.layers = 0;
	q.cover = 0.0;
	q.glow = vec3(0.0);
	q.key = vec4(0.0);
	q.shape = vec2(0.0, 0.0);
	q.content = vec2(0.0, 0.0);
	q.zone = vec2(1.0e30, -1.0e30);
	q.x = p.x;
	q.y = p.y;
	q.size = size;
	q.refractor = -1;
	q.ways = false;
	vec4 head = scene_vec(0);
	vec4 bench = scene_vec(1);
	bool on_bench = p.x >= bench.x && p.x < bench.x + bench.z && p.y >= bench.y && p.y < bench.y + bench.w;
	q.back = band_range == 1 ? 1.0 : (on_bench ? head.y : head.z);
	q.edge = 0.0;
	// the bodies of the pixel's square of the screen (the scene lists them: a pixel looks at no other)
	int square = square_at(p);
	int listed = int(scene_vec(square).x);
	for (int k = 0; k < GRID_SLOTS; k++) {
		if (k >= listed) {
			break;
		}
		int i = square_body(square, k);
		int at = HEADER_VECS + i * BODY_VECS;
		vec4 rect = scene_vec(at);
		if (hidden_at(i, p)) {
			continue;
		}
		if (p.y < rect.y - 1.0 || p.y > rect.y + rect.w + 1.0 || p.x < rect.x - 1.0 || p.x > rect.x + rect.z + 1.0) {
			continue;
		}
		if (beyond_outline(i, p, size)) {
			continue; // within its rectangle and beyond its outline (Stage 33): not its pixel
		}
		Layer l = layer_at(i, p, size, size);
		if (l.cover <= 0.0 || q.layers >= MAX_LAYERS) {
			continue;
		}
		vec4 form = scene_vec(at + 1);
		if (form.x > 5.5) {
			q.ways = true;
		}
		if (scene_vec(at + 5).w > 0.5) {
			q.refractor = i; // a vessel bends the view: the nearest the eye of those the pixel looks through
		}
		if (form.x > 2.5 && form.x < 3.5 && scene_vec(at + 2).y > 1.5) {
			q.relay = i; // a sensor's display (Stage 46)
			q.relay_k = q.layers;
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
		q.key = q.key + vec4((l.depth_cm + 3.0 * l.haze + 0.29 * l.cover) * w, (l.solid + 7.0 * l.wall + 0.13 * l.glowing) * w, ids.x, ids.y * w);
		// its temperature, where its light depends on it (Stage 46): in the long-wave, and in the visible from 500 K
		float kelvin = body_kelvin(i);
		float hottest = kelvin;
		int temps = set_of(i) * ROWS_PER_SET * ROW + TEMPS_ROW * ROW;
		if (row_value(temps) > 0.5 || row_value(temps + TEMPS_CONTENTS) > 0.5) {
			hottest = row_value(temps + TEMPS_HOTTEST); // (Stage 53: its parts' temperatures, by a signature of them)
			kelvin = row_value(temps + TEMPS_HOTTEST + 1);
		}
		if (band_range == 1 || hottest >= 500.0) {
			q.key.w = q.key.w + 0.00137 * kelvin * w;
		}
		// (10: SHAPE_BLOCK; its vec4s 7 and 8: the body's tags, and where what lies in it changed)
		vec4 tags = shape_vec(i * 10 + 7);
		vec4 changed = shape_vec(i * 10 + 8);
		q.shape = vec2(q.shape.x + tags.x * w, q.shape.y);
		q.content = vec2(q.content.x + tags.z * w, q.content.y + tags.w * w);
		q.zone = vec2(min(q.zone.x, changed.x), max(q.zone.y, changed.y));
	}
	// what glows is a light in the pass (each body's glow at 1 nm); its colour is in the key so that a pixel shows it anew
	q.key.y = q.key.y + 13.0 * q.back + 0.001 * (q.glow.x + 2.0 * q.glow.y + 3.0 * q.glow.z);
	// and the range it is read in (Stage 46): its rays go other ways there
	q.key.x = q.key.x + 0.37 * float(band_range);
	q.shape = vec2(q.shape.x + 0.41 * float(band_range), q.shape.y);
	// Where an opaque thing covers part of the pixel and no vessel bends its view, the wall shows only where it
	// is uncovered: read over those parts (four across, each by its uncovered share), once, here (Stage 22: a
	// thin line under the covered part is not seen)
	if (q.refractor < 0 && q.layers > 0 && opaque_edge(q)) {
		int profile = wall_profile(q.y);
		float sum = 0.0;
		float open = 0.0;
		for (int c = 0; c < 4; c++) {
			float left = q.x - 0.5 * size + 0.25 * size * float(c);
			float u = uncovered(q, c);
			sum = sum + u * wall_span(profile, left, left + 0.25 * size);
			open = open + u;
		}
		if (open > 1.0e-6) {
			q.back = sum / open;
			q.edge = 1.0;
		}
	}
	if (q.refractor >= 0) {
		// What its rays can reach beyond the bodies over the pixel itself: those that stand behind the vessel, in
		// its rows, on a layer further from the eye. The scene's signature of them (where they stand, what they
		// are and hold) is in the key, so a pixel whose rays reach a body that has changed colour is read again;
		// and their shapes are in the shape key: a pixel whose rays may go another way is traced again.
		vec4 behind_changed = shape_vec(q.refractor * 10 + 8);
		vec4 behind_zone = shape_vec(q.refractor * 10 + 9);
		q.shape = vec2(q.shape.x, shape_vec(q.refractor * 10 + 7).y);
		q.content = vec2(q.content.x + 1.37 * behind_changed.z, q.content.y + 1.37 * behind_changed.w);
		q.zone = vec2(min(q.zone.x, behind_zone.x), max(q.zone.y, behind_zone.y));
		q.key.w = q.key.w + 0.73 * scene_vec(HEADER_VECS + q.refractor * BODY_VECS + 7).z + 0.61 * q.shape.x + 0.47 * q.shape.y + 0.53 * q.content.x;
	}
	// (Stage 61) within a body's outline: every wall it reads, the wall behind glass
	wall_base = q.layers > 0 ? int(scene_vec(5).w) : 0;
	return q;
}

// whether any body's rectangle is near the pixel: most of the screen is not, and needs nothing more
bool near_any(vec2 p) {
	return scene_vec(square_at(p)).x > 0.5;
}

// ---- what gets through at one wavelength

float row_at(int start, float nm) {
	if (nm > 7000.0) {
		// the long-wave segment (Stage 46): row (start / ROW) of the long-wave rows, which begin at the header's base
		float y = clamp((nm - 8000.0) / 20.0, 0.0, 300.0);
		int j = min(int(floor(y)), 299);
		int base = int(scene_vec(0).w) + (start / ROW) * LWIR_ROW;
		return row_value(base + j) * (1.0 - (y - float(j))) + row_value(base + j + 1) * (y - float(j));
	}
	float x = clamp(nm - 380.0, 0.0, 400.0);
	int i = min(int(floor(x)), 399);
	float f = x - float(i);
	return row_value(start + i) * (1.0 - f) + row_value(start + i + 1) * f;
}

// what a body's haze at height y turns back at nm: its solids' reflectance and its bubbles' albedo, by their shares
float haze_colour(int body, int start, float y, float nm) {
	return mix(row_at(start + ROW, nm), row_at(start + BUBBLE_ROW * ROW, nm), white_at(body, y));
}

// The shapes' head (Stage 30; lab/optics_scene.hpp lays it out): SHAPE_BLOCK vec4s a body: its shape's start,
// whether it is made as glassware, whether what lies in it is given, its turn; the turn's pivot, cosine and
// sine; its rectangle upright; the room's up in its frame, whether it holds liquid, the liquid's level; its
// vortex's depth, half width and half depth, whether it holds sediment; the vortex's middle, the sediment's
// level, its pile's height; the pile's half width and half depth, its middle.
const int SHAPE_BLOCK = 10;

// A layered vessel's band at height y of the screen (Stage 47: its column's layers, GAME_SPEC §6): the start of
// that band's set, whose liquid row is its layer's; a vessel of one layer, its own. The bands lie level in the room
// at any turn, as the liquid's surface does: a place's level is read in the vessel's frame as the surface's is, so
// many cm up the room from the frame's origin (the middle of its upright bottom, turned about its pivot).
int band_rows(int body, int rows, float y) {
	float n = row_value(rows + BANDS_ROW * ROW);
	if (n < 0.5) {
		return rows;
	}
	vec4 turned = shape_vec(body * SHAPE_BLOCK + 1); // its pivot, the turn's cosine and sine
	vec4 upright = shape_vec(body * SHAPE_BLOCK + 2); // its rectangle upright
	float origin = turned.y + (upright.x + 0.5 * upright.z - turned.x) * turned.w + (upright.y + upright.w - turned.y) * turned.z;
	float z = (origin - y) * scene_vec(HEADER_VECS + body * BODY_VECS + 1).y; // its level, cm
	int count = int(n);
	int found = rows;
	for (int k = 0; k < 133; k++) {
		if (k >= count) {
			break;
		}
		int e = rows + BANDS_ROW * ROW + 1 + 3 * k;
		found = int(row_value(e)) * ROWS_PER_SET * ROW;
		if (z < row_value(e + 2)) {
			break;
		}
	}
	return found;
}

float liquid_at(int body, int rows, float nm, float y) {
	int band = band_rows(body, rows, y);
	float depth = row_at(band, nm);
	if (nm > 7000.0 || band != rows) {
		return depth; // (narrow lines are the visible's; a layer's band: its narrow lines are not drawn, GAME_SPEC §20)
	}
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

// ---- the room's faces, ruled as the wall is (GAME_SPEC §12.3, Stage 78)
//
// A ray that comes to a face of the room's cube off its black edge cells (the front, the sides, the top and the
// bottom: a mirror shows them) is put at FACE_X + face * FACE_STRIDE + u, FACE_Y + v: the face's own place, from its
// corner (lab/optics_scene.hpp). The header's ninth vec4 says how each is ruled: its white, its lines' radiance, a
// line's width and the pitch (0: plain, each face its mean as the wall's table has it, far off to the right). Read
// exactly, as the wall's table is: a row's lines across by how much of it they cover (its profile, below 0), and the
// lines down the row in closed form.
const float FACE_X = 131072.0;
const float FACE_STRIDE = 65536.0;
const float FACE_Y = 131072.0;

bool faces_ruled() {
	return scene_vec(8).w > 0.0;
}

// how much of the way from 0 to a, along one way of a face, its lines cover
float face_lines(float a) {
	vec4 f = scene_vec(8);
	float n = floor(a / f.w);
	return n * f.z + clamp(a - n * f.w, 0.0, f.z);
}

// the black of the cube's edge cells (the wall's table, far off to the left)
float face_edge() {
	int m = int(wall_of(0).x);
	return wall_of(WALL_BREAK_BASE + m + int(wall_of(WALL_ROW_BASE).x) * (m + 1)).x; // (a row's first cell, far left)
}

// A face's row from y0 to y1 (the face's place, as put): its profile, below -1: how much of it lies in the black
// cells along the face's edges (b, of 64) and how much of the rest its lines across cover (a, of 64), as
// -2 - (a + 65 b)
int face_profile(float y0, float y1) {
	vec4 f = scene_vec(8);
	float side = scene_vec(3).z;
	float cell = scene_vec(3).w;
	float v0 = min(y0, y1) - FACE_Y;
	float v1 = max(y0, y1) - FACE_Y;
	float m0 = clamp(v0, cell, side - cell);
	float m1 = clamp(v1, cell, side - cell);
	float b = 0.0;
	float a = 0.0;
	if (v1 - v0 > 1.0e-5) {
		b = 1.0 - (m1 - m0) / (v1 - v0);
		a = m1 - m0 > 1.0e-5 ? (face_lines(m1) - face_lines(m0)) / (m1 - m0) : 0.0;
	} else {
		b = v0 < cell || v0 > side - cell ? 1.0 : 0.0;
		a = v0 - floor(v0 / f.w) * f.w < f.z ? 1.0 : 0.0;
	}
	return -2 - (int(floor(a * 64.0 + 0.5)) + 65 * int(floor(b * 64.0 + 0.5)));
}

// where along its face a place is put (u), and where its face begins
float face_origin(float x) {
	return FACE_X + floor((x - FACE_X) / FACE_STRIDE) * FACE_STRIDE;
}

// the integral along a face's row (its lines across covering `across` of it) from u0 to u1, u1 at least u0: the
// black cells at each end of it, its lines down it between
float face_integral(float across, float u0, float u1) {
	vec4 f = scene_vec(8);
	float side = scene_vec(3).z;
	float cell = scene_vec(3).w;
	float lit = mix(f.x, f.y, across);
	float m0 = clamp(u0, cell, side - cell);
	float m1 = clamp(u1, cell, side - cell);
	float middle = lit * (m1 - m0) + (f.y - lit) * (face_lines(m1) - face_lines(m0));
	return middle + face_edge() * ((u1 - u0) - (m1 - m0));
}

// the mean along a face's row of profile `profile` from a to b
float face_span(int profile, float a, float b) {
	vec4 f = scene_vec(8);
	float side = scene_vec(3).z;
	float cell = scene_vec(3).w;
	int k = -2 - profile;
	float across = float(k - 65 * (k / 65)) / 64.0;
	float edged = float(k / 65) / 64.0;
	float lo = min(a, b);
	float hi = max(a, b);
	float origin = face_origin(lo);
	float u0 = lo - origin;
	float u1 = hi - origin;
	float mean = 0.0;
	if (u1 - u0 < 1.0e-5) {
		bool edge = u0 < cell || u0 > side - cell;
		bool on = u0 - floor(u0 / f.w) * f.w < f.z;
		mean = edge ? face_edge() : (on ? f.y : mix(f.x, f.y, across));
	} else {
		mean = face_integral(across, u0, u1) / (u1 - u0);
	}
	return mix(mean, face_edge(), edged);
}

// swept_wall on a face: its footprint's two ends move from a0 and a to b0 and b; the mean over the sweep of what
// lies between them, at eight places along it (each read exactly)
float face_swept(int profile, float a0, float a, float b0, float b) {
	float w0 = a - a0;
	float w1 = b - b0;
	if (w0 * w1 < 0.0) {
		return 0.5 * (face_span(profile, a0, a) + face_span(profile, b0, b));
	}
	if (abs(w0 + w1) < 2.0e-4) {
		return face_span(profile, 0.5 * (a0 + a), 0.5 * (b0 + b));
	}
	float sum = 0.0;
	float widths = 0.0;
	for (int i = 0; i < 8; i++) {
		float t = (float(i) + 0.5) / 8.0;
		float lo = mix(a0, b0, t);
		float hi = mix(a, b, t);
		float w = abs(hi - lo);
		sum = sum + w * face_span(profile, lo, hi);
		widths = widths + w;
	}
	return widths > 0.0 ? sum / widths : face_span(profile, 0.5 * (a0 + b0), 0.5 * (a + b));
}

// ---- the wall behind the bodies (GAME_SPEC §12.3): a table, read exactly


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

// the profile of the row a place is on: a row of the screen, the row over it or the row under it (a ray that
// lands beyond the screen reads the wall as it is just past the screen's edge, Stage 30)
int wall_profile(float y) {
	if (y >= 0.5 * FACE_Y) {
		return face_profile(floor(y), floor(y) + 1.0); // (Stage 78) a face's row
	}
	return int(wall_of(WALL_ROW_BASE + clamp(int(floor(y)), -1, WALL_ROWS)).x);
}

WallAt wall_at(int profile, float x) {
	int m = int(wall_of(0).x);
	// the cell: how many breakpoints are at or left of x; from the column's, a few steps
	int j = x < 0.0 ? 0 : int(wall_of(WALL_COLUMN_BASE + min(int(x), WALL_COLUMNS - 1)).x);
	float right = x; // not known: a ray that moves looks again
	for (int k = 0; k < 64; k++) {
		if (j >= m) {
			right = 1.0e30;
			break;
		}
		float next = wall_of(WALL_BREAK_BASE + j).x;
		if (next > x) {
			right = next;
			break;
		}
		j = j + 1;
	}
	vec4 c = wall_of(WALL_BREAK_BASE + m + profile * (m + 1) + j);
	WallAt w;
	w.cell = j;
	w.base = wall_of(WALL_BREAK_BASE + max(j - 1, 0)).x;
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
	if (band_range == 1) {
		return 1.0; // the long-wave room is one black body (Stage 46): the wall is its light, wherever it is read
	}
	if (profile <= -2) {
		return face_span(profile, a, b); // (Stage 78)
	}
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
	int m = int(wall_of(0).x);
	float sum = 0.0;
	float at = lo;
	float s_at = s_a;
	for (int k = 0; k < 4096; k++) {
		int cell = a.cell + k;
		if (cell > b.cell) {
			break;
		}
		float end = cell == b.cell ? hi : wall_of(WALL_BREAK_BASE + cell).x; // a cell ends at its own breakpoint
		float s_end = s_at + wall_of(WALL_BREAK_BASE + m + profile * (m + 1) + cell).x * (end - at);
		sum = sum + 0.5 * (s_at + s_end) * (end - at);
		at = end;
		s_at = s_end;
	}
	return sum / (hi - lo);
}

// ---- a place in the spectrum

// where a wavelength is along the spectrum: 0 at 380 nm, 1 at 780 nm, even in 1 / nm^2 (what glass and
// water disperse nearly straight in)
float map_place(float nm) {
	if (band_range == 1) {
		// the long-wave range (Stage 46): 0 at 8000 nm, 1 at 14000 nm, alike
		float l = 8000.0 / clamp(nm, 8000.0, 14000.0);
		return clamp((1.0 - l * l) / 0.6734693878, 0.0, 1.0);
	}
	float r = 380.0 / clamp(nm, 380.0, 780.0);
	return clamp((1.0 - r * r) / 0.7626561473, 0.0, 1.0);
}

// the cubic through four values at the thirds of a stretch, at g of the way through it: what each value weighs
vec4 thirds_weights(float g) {
	float s = 3.0 * g;
	return vec4(-(s - 1.0) * (s - 2.0) * (s - 3.0) / 6.0, s * (s - 2.0) * (s - 3.0) / 2.0, -s * (s - 1.0) * (s - 3.0) / 2.0, s * (s - 1.0) * (s - 2.0) / 6.0);
}

// ---- what reaches the eye

// The chord that absorbs as the rays of a part do, together (GAME_SPEC §12.3): their chords run from c0 to
// c1, the square straight across the part (as a chord's is, exactly, where it rises as a root at a rim),
// and the liquid's optical depth is `per_cm` a centimetre. The mean of e^(-per_cm c) over the part, in closed
// form, as one chord's.
// How much of the room's light what is stirred up in a vessel's liquid turns back, along a path through the
// liquid as long as `chord_cm`: its extinction (MORE.x, a centimetre) over that path (Stage 26: along the path its
// ray takes, the vessel's own shape, not its rectangle as a cylinder: a conical flask's stirred suspension was a
// grey rectangle past the cone's sides)
float haze_along(int body, float chord_cm, float y) {
	int at = HEADER_VECS + body * BODY_VECS;
	return scene_vec(at + 1).x < 0.5 ? 1.0 - exp(-(scene_vec(at + 2).x + bubbles_at(body, y)) * max(chord_cm, 0.0)) : 0.0;
}

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
	float colour; // what its solids turn back
	float scatter; // what its haze turns back: its solids' colour and its bubbles' albedo, by their shares (Stage 45)
	float glow; // what its solids glow with
	float glass; // what its wall lets through
	float haze;
	float solid;
	float room; // the room in front, which the surfaces turn back
	int body; // the refracting one's
	float theta; // what it emits by (Stage 46): its contents
	float theta_wall; // and its wall where it is crossed (Stage 53)
};

// ---- what a body emits by its temperature (GAME_SPEC §12.4, Stage 46: Kirchhoff)

// the radiance of the room in front, which the surfaces turn back: in the long-wave the room's black body, 1
float room_front() {
	return band_range == 1 ? 1.0 : scene_vec(2).y;
}

// A black body's radiance at `kelvin` over the light's at nm: in the long-wave the ratio of two black bodies' (the
// room is one: exactly 1 at its temperature); in the visible Planck's law over the room's light there, nothing below
// 500 K (not a millionth of the room's light).
float theta_of(float nm, float kelvin) {
	float room = scene_vec(4).z;
	if (nm > 7000.0) {
		if (kelvin == room) {
			return 1.0;
		}
		float c = 14387769.0 / nm; // the second radiation constant, nm K, over the wavelength
		return (exp(c / room) - 1.0) / (exp(c / max(kelvin, 1.0)) - 1.0);
	}
	if (kelvin < 500.0) {
		return 0.0;
	}
	float x = clamp(nm - 380.0, 0.0, 400.0);
	int i = min(int(floor(x)), 399);
	float light = mix(light_vec(LIGHT_ENTRIES + i).y, light_vec(LIGHT_ENTRIES + i + 1).y, x - float(i));
	float m = nm * 1.0e-3; // micrometres
	// 2 h c^2 / wavelength^5 / (e^(c2 / wavelength T) - 1), W / m2 / sr / nm
	return 1.191042972e5 / (m * m * m * m * m) / (exp(14387.769 / (m * kelvin)) - 1.0) / max(light, 1.0e-30);
}

// A vessel's temperature at a height (Stage 53), from a list in its TEMPS row: a count, then (height, K) pairs
// rising: linear between them, held beyond the first and the last
float profile_at(int base, float z) {
	int n = int(row_value(base));
	float lo_z = row_value(base + 1);
	float lo_k = row_value(base + 2);
	if (n < 2 || z <= lo_z) {
		return lo_k;
	}
	for (int k = 1; k < 133; k++) {
		if (k >= n) {
			break;
		}
		float hz = row_value(base + 1 + 2 * k);
		float hk = row_value(base + 2 + 2 * k);
		if (z <= hz) {
			return mix(lo_k, hk, (z - lo_z) / max(hz - lo_z, 1.0e-6));
		}
		lo_z = hz;
		lo_k = hk;
	}
	return lo_k;
}

// What a body emits by at a place p of the screen (Stage 53, GAME_SPEC §12.4): its contents' (x) and its wall's
// (y) Planck ratios. A vessel's wall is its rings, each at its own temperature, read along its axis (the place
// turned back to where it stands upright, so many cm over its base); its contents are its layers, each at its
// own, read by the level in the room (as its bands lie, band_rows). Both run continuously between their parts'
// middles: heat crosses from each to the next, and a temperature has no step there. A body without them emits
// by its one temperature; in the visible nothing under 500 K emits, so a vessel no part of which is that hot
// reads nothing more.
vec2 heat_of(int body, int start, vec2 p, float nm) {
	int temps = start + TEMPS_ROW * ROW;
	float walls = row_value(temps);
	float held = row_value(temps + TEMPS_CONTENTS);
	if (walls < 0.5 && held < 0.5) {
		float t = theta_of(nm, body_kelvin(body));
		return vec2(t, t);
	}
	if (nm < 7000.0 && row_value(temps + TEMPS_HOTTEST) < 500.0) {
		return vec2(0.0, 0.0);
	}
	vec4 turned = shape_vec(body * SHAPE_BLOCK + 1); // its pivot, the turn's cosine and sine
	vec4 upright = shape_vec(body * SHAPE_BLOCK + 2); // its rectangle upright
	float cm = scene_vec(HEADER_VECS + body * BODY_VECS + 1).y;
	float k_in = body_kelvin(body);
	if (held > 0.5) {
		float origin = turned.y + (upright.x + 0.5 * upright.z - turned.x) * turned.w + (upright.y + upright.w - turned.y) * turned.z;
		k_in = profile_at(temps + TEMPS_CONTENTS, (origin - p.y) * cm);
	}
	float k_wall = k_in;
	if (walls > 0.5) {
		float dx = p.x - turned.x;
		float dy = p.y - turned.y;
		k_wall = profile_at(temps, (upright.y + upright.w - (turned.y - turned.w * dx + turned.z * dy)) * cm);
	}
	return vec2(theta_of(nm, k_in), theta_of(nm, k_wall));
}

// ---- an image a thing emits by (GAME_SPEC §12.1: image weights; Stage 42, the spectral viewer's screen)
//
// A screen is a slab whose set's glow is -1 (no spectrum is less than nothing, O1): its light is its image's. Each
// pixel of the image is three subpixels side by side, red, green and blue, each a third of it across and `fill`
// of that lit, `fill` of it lit down; each lit by its weight (linear, 0 to 1) times its channel's light. Read
// exactly over any sample, from each row's running sums of its weights (lab/optics_scene.hpp: images).

// channel c's light at nm, of the image whose head is at img: a multiple of the room's light
float image_channel(int img, int c, float nm) {
	float x = clamp(nm - 380.0, 0.0, 400.0);
	int i = min(int(floor(x)), 399);
	int at = int(image_vec(img + 1).x) * 4 + c * ROW + i;
	float a = image_vec(at / 4)[at % 4];
	float b = image_vec((at + 1) / 4)[(at + 1) % 4];
	return mix(a, b, x - float(i));
}

// what one row of the image gives over [u0, u1] (its pixels' units, within the image): each channel's weights
// times how much of each of its subpixels the span covers
vec3 image_row_over(int img, int row, float u0, float u1) {
	vec4 head = image_vec(img);
	int w = int(head.x);
	float gap = (1.0 - head.z) / 6.0; // each side of a subpixel's third that is dark
	int sums = int(image_vec(img + 1).y) + row * (w + 1);
	int ia = clamp(int(floor(u0)), 0, w - 1);
	int ib = clamp(int(floor(u1)), 0, w - 1);
	vec4 pa = image_vec(sums + ia);
	vec4 pa1 = image_vec(sums + ia + 1);
	vec3 wa = vec3(pa1.x - pa.x, pa1.y - pa.y, pa1.z - pa.z);
	float fa = float(ia);
	vec3 oa = vec3(max(min(u1, fa + 1.0 / 3.0 - gap) - max(u0, fa + gap), 0.0), max(min(u1, fa + 2.0 / 3.0 - gap) - max(u0, fa + 1.0 / 3.0 + gap), 0.0),
			max(min(u1, fa + 1.0 - gap) - max(u0, fa + 2.0 / 3.0 + gap), 0.0));
	if (ib == ia) {
		return vec3(wa.x * oa.x, wa.y * oa.y, wa.z * oa.z);
	}
	vec4 pb = image_vec(sums + ib);
	vec4 pb1 = image_vec(sums + ib + 1);
	vec3 wb = vec3(pb1.x - pb.x, pb1.y - pb.y, pb1.z - pb.z);
	float fb = float(ib);
	vec3 ob = vec3(max(min(u1, fb + 1.0 / 3.0 - gap) - max(u0, fb + gap), 0.0), max(min(u1, fb + 2.0 / 3.0 - gap) - max(u0, fb + 1.0 / 3.0 + gap), 0.0),
			max(min(u1, fb + 1.0 - gap) - max(u0, fb + 2.0 / 3.0 + gap), 0.0));
	// the pixels between, whole: each subpixel a third of the pixel less its gaps
	float whole = 1.0 / 3.0 - 2.0 * gap;
	return vec3(wa.x * oa.x + wb.x * ob.x + (pb.x - pa1.x) * whole, wa.y * oa.y + wb.y * ob.y + (pb.y - pa1.y) * whole, wa.z * oa.z + wb.z * ob.z + (pb.z - pa1.z) * whole);
}

// A thing's image's light over the sample `across` by `up` about p, at nm: a multiple of the room's light over
// the part of the sample the thing covers (`cover`: what its layer's solid is, which its glow is weighed by). What
// the sample takes of each channel does not depend on the wavelength: it is found once for a sample and read again
// for each of its bands (a pixel reads its image's sample for every band of every point; Stage 42)
#ifndef OPTICS_THREAD
#define OPTICS_THREAD
#endif
OPTICS_THREAD vec4 image_sampled = vec4(-1.0, 0.0, 0.0, 0.0); // body, p, the sample's size
OPTICS_THREAD vec4 image_taken = vec4(0.0); // what it takes of each channel, as a share of the covered sample; its image
float image_glow(int body, vec2 p, float across, float up, float nm, float cover) {
	if (cover <= 0.0) {
		return 0.0;
	}
	if (image_sampled.x == float(body) && image_sampled.y == p.x && image_sampled.z == p.y && image_sampled.w == across * 4096.0 + up * cover) {
		int img = int(image_taken.w);
		return image_taken.x * image_channel(img, 0, nm) + image_taken.y * image_channel(img, 1, nm) + image_taken.z * image_channel(img, 2, nm);
	}
	int at = HEADER_VECS + body * BODY_VECS;
	vec4 rect = scene_vec(at);
	int img = int(scene_vec(at + 2).x);
	vec4 head = image_vec(img);
	float sx = head.x / rect.z; // its pixels to a pixel of the screen
	float sy = head.y / rect.w;
	float u0 = clamp((p.x - 0.5 * across - rect.x) * sx, 0.0, head.x);
	float u1 = clamp((p.x + 0.5 * across - rect.x) * sx, 0.0, head.x);
	float v0 = clamp((p.y - 0.5 * up - rect.y) * sy, 0.0, head.y);
	float v1 = clamp((p.y + 0.5 * up - rect.y) * sy, 0.0, head.y);
	float gap = 0.5 * (1.0 - head.w);
	int r0 = clamp(int(floor(v0)), 0, int(head.y) - 1);
	int r1 = clamp(int(floor(v1)), 0, int(head.y) - 1);
	vec3 lit = vec3(0.0);
	for (int k = 0; k < 4096; k++) {
		int row = r0 + k;
		if (row > r1) {
			break;
		}
		float fr = float(row);
		float down = max(min(v1, fr + 1.0 - gap) - max(v0, fr + gap), 0.0);
		if (down > 0.0) {
			lit = lit + image_row_over(img, row, u0, u1) * down;
		}
	}
	// the sample's area in the image's pixels: what was lit, as a share of all of it, then of its covered part
	float share = 1.0 / (max(across * sx * up * sy, 1.0e-20) * cover);
	image_sampled = vec4(float(body), p.x, p.y, across * 4096.0 + up * cover);
	image_taken = vec4(share * lit.x, share * lit.y, share * lit.z, float(img));
	return image_taken.x * image_channel(img, 0, nm) + image_taken.y * image_channel(img, 1, nm) + image_taken.z * image_channel(img, 2, nm);
}

// what a layer's solid glows with at nm (a multiple of the room's light): its set's row, or where that is -1,
// its image's light over the sample (`cover`, the layer's solid)
float glow_of(int body, int start, vec2 p, float across, float up, float nm, float cover) {
	float glow = row_at(start + 5 * ROW, nm);
	return glow < 0.0 ? image_glow(body, p, across, up, nm, cover) : glow;
}

Shade shade_layer(Shade s, Path q, Layer l, int k, float nm) {
	if (k >= q.layers) {
		return s;
	}
	int start = set_in(l.body);
	float per_cm = liquid_at(body_of(l.body), start, nm, q.y);
	float colour = row_at(start + ROW, nm);
	float glass = exp(-row_at(start + 2 * ROW, nm) * l.wall);
	float glow = glow_of(body_of(l.body), start, vec2(q.x, q.y), q.size, q.size, nm, l.solid); // what its solids glow with, a multiple of the room's light
	vec2 heat = heat_of(body_of(l.body), start, vec2(q.x, q.y), nm);
	if (body_of(l.body) == q.refractor && !s.own) {
		s.own = true;
		s.theta = heat.x;
		s.theta_wall = heat.y;
		s.per_cm = per_cm;
		s.colour = colour;
		s.scatter = mix(colour, row_at(start + BUBBLE_ROW * ROW, nm), white_at(body_of(l.body), q.y));
		s.glow = glow;
		s.glass = glass;
		s.haze = l.haze;
		s.solid = l.solid;
		s.body = body_of(l.body);
		return s;
	}
	// what one body does to the light that reaches it (Beer-Lambert through its liquid; what is suspended in it
	// turns the room's light back from half way in; what lies in it is lit and seen there and back; it glows; and
	// its wall), as a times what reaches it plus b
	float depth = per_cm * (l.depth_cm + l.glowing); // (a glowing thing's absorption is per pixel of its path: Stage 46)
	float through = exp(-depth);
	float a = through * (1.0 - l.haze) * (1.0 - l.solid) * glass;
	float scatter = mix(colour, row_at(start + BUBBLE_ROW * ROW, nm), white_at(body_of(l.body), q.y)); // what its haze turns back
	float b = (scatter * exp(-depth * 0.5) * l.haze * (1.0 - l.solid) + colour * through * l.solid + glow * (exp(-depth * 0.5) * l.solid + l.glowing)) * glass;
	// (Stage 53) what its contents neither pass nor turn back they emit at their temperature, what its wall takes
	// in at the wall's
	b = b + heat.x * (glass - a - (scatter * exp(-depth * 0.5) * l.haze * (1.0 - l.solid) + colour * through * l.solid) * glass) + heat.y * (1.0 - glass);
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
	s.scatter = 0.0;
	s.glow = 0.0;
	s.glass = 1.0;
	s.haze = 0.0;
	s.solid = 0.0;
	s.body = 0;
	s.room = room_front();
	s.theta = 0.0;
	s.theta_wall = 0.0;
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
		// its haze along these rays' own path through its liquid; none through its core as air
		float k = scene_vec(HEADER_VECS + s.body * BODY_VECS + 2).x + bubbles_at(s.body, q.y);
		float haze = liquid ? haze_along(s.body, chord_of_part(c0, c1, max(k, 1.0e-6)), q.y) : 0.0;
		seen = mix(seen, s.scatter * exp(-depth * 0.5), haze);
		seen = mix(seen, s.colour * through + s.glow * exp(-depth * 0.5), s.solid);
		seen = seen * s.glass;
		// (Stage 46) what it passes and what it turns back, and the rest it emits
		float own_a = through * (1.0 - haze) * (1.0 - s.solid) * s.glass;
		float own_r = ((s.scatter * exp(-depth * 0.5) * haze) * (1.0 - s.solid) + s.colour * through * s.solid) * s.glass;
		seen = seen + s.theta * (s.glass - own_a - own_r) + s.theta_wall * (1.0 - s.glass); // (Stage 53: its contents', its wall's)
	}
	if (q.refractor >= 0) {
		seen = pass * seen + (1.0 - pass) * s.room;
	}
	return s.a_front * seen + s.b_front;
}

float along(Path q, float nm, float pass, float c0, float c1, bool liquid, float wall) {
	return shaded(shade_of(q, nm), q, pass, c0, c1, liquid, wall);
}

// in how many straight steps a band of `nm` nanometres is swept, where the landing bends so: as many as keep
// the sweep within 0.02 pixels of the screen of the true landing; at most sixteen
int map_steps(float bend, float nm) {
	return int(clamp(ceil(nm * sqrt(bend / 0.16)), 1.0, 16.0));
}

vec4 share(float u);

// the wavelength at the share u of the light's energy; or `one_nm`, if one wavelength alone is asked for
float nm_at(float u, float one_nm) {
	return one_nm > 0.0 ? one_nm : share(u).x + 380.0;
}

// the share of the light's energy up to a wavelength: the table the other way
float share_of(float nm) {
	if (band_range == 1) {
		// the long-wave light's (Stage 46): at each 20 nm from 8000 nm
		float y = clamp((nm - 8000.0) / 20.0, 0.0, 300.0);
		int j = min(int(floor(y)), 299);
		return mix(light_vec(LIGHT_SIZE + LIGHT_ENTRIES + j).x, light_vec(LIGHT_SIZE + LIGHT_ENTRIES + j + 1).x, y - float(j));
	}
	float x = clamp(nm - 380.0, 0.0, 400.0);
	int i = min(int(floor(x)), 399);
	return mix(light_vec(LIGHT_ENTRIES + i).x, light_vec(LIGHT_ENTRIES + i + 1).x, x - float(i));
}

// what reaches the eye where no vessel bends the view: the bodies as they lie, before the backdrop
float flat_band(Path q, float u0, float u1, float t, float one_nm) {
	// the wall itself over the pixel's width (Stage 22: a flame and a balloon are seen through, and the bench's
	// drawing with them)
	int profile = wall_profile(q.y);
	float wall = q.edge > 0.5 ? q.back : wall_span(profile, q.x - 0.5 * q.size, q.x + 0.5 * q.size); // at an opaque thing's edge, where it leaves the wall uncovered
	return along(q, nm_at(mix(u0, u1, t), one_nm), 1.0, 0.0, 0.0, false, wall);
}

// The wall over a part's footprint as a step of a band's wavelengths is crossed: its two rays land at a0 and
// a at the step's beginning, at b0 and b at its end. The mean, along each ray's sweep, of the row's integral,
// the footprint's width taken as its mean: exact, however many edges the sweep crosses.
float swept_wall(int profile, float a0, float a, float b0, float b) {
	if (band_range == 1) {
		return 1.0;
	}
	if (profile <= -2) {
		return face_swept(profile, a0, a, b0, b); // (Stage 78)
	}
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
// (Stage 46) What it neither passes nor turns away it emits, `theta` times (Kirchhoff, GAME_SPEC §12.4); and of what
// its surfaces do not pass, `absorbed` was taken in by its glass, which emits as much, the rest turned away.
// (Stage 53) Its contents emit by heat.x, its wall (what its skin and its surfaces take in) by heat.y.
vec2 body_does(Layer l, float colour, float scatter, float glow, float skin_depth, float depth, float pass, float absorbed, float room, vec2 heat) {
	float skin = exp(-skin_depth * l.wall);
	float through = exp(-depth);
	float a = through * (1.0 - l.haze) * (1.0 - l.solid) * skin;
	float b = (scatter * exp(-depth * 0.5) * l.haze * (1.0 - l.solid) + colour * through * l.solid + glow * (exp(-depth * 0.5) * l.solid + l.glowing)) * skin;
	float r = (scatter * exp(-depth * 0.5) * l.haze * (1.0 - l.solid) + colour * through * l.solid) * skin;
	b = b + heat.x * (skin - a - r) + heat.y * (1.0 - skin);
	return vec2(pass * a, pass * b + (1.0 - pass - absorbed) * room + absorbed * heat.y);
}



// ---- rays traced live (GAME_SPEC §12.3, Stage 29)
//
// A ray followed here, meeting by meeting, through each body's own surfaces: nothing made ahead. The includer's
// shape_vec lays the surfaces out (lab/optics_scene.hpp: the shapes): [b], b < MAX_BODIES: where body b's shape
// begins (x), or -1; whether it is made as glassware (y). Then each shape: [s] its patches (x) and how far
// from its axis it reaches (y; Stage 33), and six vec4s a
// patch: a and c; l and whether it is a torus; its heights and its rings (lo.y, hi.y, rho_lo, rho_hi); its box
// (lo.x, hi.x, lo.z, hi.z); a torus's major, cy, minor, t_lo; its t_hi, the medium where f < 0, the one where
// f > 0. The method is the truth's own tracer's (lab/optics_trace.hpp), step for step, so the two can be held
// to each other ray by ray; a ray keeps a fixed, small state (no arrays).

const int SHAPE_PATCH = 6;
// The meetings a ray is allowed in one shape, and how often it may be turned back in it (Stage 33;
// lab/optics_trace.hpp: MEETINGS_MOST, TURNS_MOST, and why). After either it goes straight on.
const int TRACE_MOST = 16;
const int TRACE_TURNS = 4;

vec3 ray_point(vec3 o, vec3 u, float t) {
	return vec3(o.x + t * u.x, o.y + t * u.y, o.z + t * u.z);
}

float dot3(vec3 a, vec3 b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}


// whether what lies in a body is given (its liquid's surface, its sediment): then its core is traced by it
bool lies_in(int body) {
	return shape_vec(body * SHAPE_BLOCK).z > 0.5;
}

// What is at a place in a body's core (Stage 30): its liquid (2), the air over it (3), or its sediment (4). Its
// liquid lies under a level of the room, less the vortex's cone hanging from that surface; its sediment under
// another, and in the pile's cone standing on it. A cone's base is an ellipse about its middle, as wide as the
// level's line on the screen and reaching into the depth.
bool inside_cone(vec3 p, vec2 up, vec2 middle, float across, float deep, float narrowing) {
	if (!(narrowing >= 0.0) || !(across > 0.0) || !(deep > 0.0)) {
		return false;
	}
	float q = ((p.x - middle.x) * up.y - (p.y - middle.y) * up.x) / across;
	float w = p.z / deep;
	return q * q + w * w <= narrowing * narrowing;
}

int inside_medium(int block, vec3 p) {
	vec4 b3 = shape_vec(block + 3);
	vec4 b4 = shape_vec(block + 4);
	vec4 b5 = shape_vec(block + 5);
	vec4 b6 = shape_vec(block + 6);
	vec2 up = vec2(b3.x, b3.y);
	float level = up.x * p.x + up.y * p.y;
	if (b4.w > 0.5 && (level <= b5.z || (b5.w > 0.0 && level - b5.z <= b5.w && inside_cone(p, up, vec2(b6.z, b6.w), b6.x, b6.y, 1.0 - (level - b5.z) / b5.w)))) {
		return 4;
	}
	if (b3.z > 0.5 && level <= b3.w && !(b4.x > 0.0 && b3.w - level <= b4.x && inside_cone(p, up, vec2(b5.x, b5.y), b4.y, b4.z, 1.0 - (b3.w - level) / b4.x))) {
		return 2;
	}
	return 3;
}

// a place along a ray within a core, if what the ray is in changes there and it is nearer than `best` (x: the
// nearest so far; y: which surface it is on)
vec2 inside_nearer(int block, vec3 o, vec3 u, float t, float which, vec2 best) {
	if (t > TRACE_NEAR && t < best.x && inside_medium(block, ray_point(o, u, t - TRACE_NUDGE)) != inside_medium(block, ray_point(o, u, t + TRACE_NUDGE))) {
		return vec2(t, which);
	}
	return best;
}

// the two places a ray meets a cone (q^2 + w^2 = h^2, each straight along the ray): x and y, or -1
vec2 cone_meets(vec3 o, vec3 u, vec2 up, vec2 middle, float across, float deep, float h0, float h1) {
	if (!(across > 0.0) || !(deep > 0.0)) {
		return vec2(-1.0, -1.0);
	}
	float q0 = ((o.x - middle.x) * up.y - (o.y - middle.y) * up.x) / across;
	float q1 = (u.x * up.y - u.y * up.x) / across;
	float w0 = o.z / deep;
	float w1 = u.z / deep;
	float a = q1 * q1 + w1 * w1 - h1 * h1;
	float b = 2.0 * (q0 * q1 + w0 * w1 - h0 * h1);
	float c = q0 * q0 + w0 * w0 - h0 * h0;
	if (abs(a) < 1.0e-14) {
		return vec2(abs(b) > 1.0e-14 ? -c / b : -1.0, -1.0);
	}
	float disc = b * b - 4.0 * a * c;
	if (disc < 0.0) {
		return vec2(-1.0, -1.0);
	}
	float q = -0.5 * (b + (b < 0.0 ? -1.0 : 1.0) * sqrt(disc));
	return vec2(q / a, q != 0.0 ? c / q : -1.0);
}

// The nearest place before `within` where a ray in a core crosses from one thing into another (x; -1: none),
// and which surface that is (y: 0 the liquid's level, 1 the sediment's, 2 the vortex's cone, 3 the pile's).
vec2 inside_crossing(int block, vec3 o, vec3 u, float within) {
	vec4 b3 = shape_vec(block + 3);
	vec4 b4 = shape_vec(block + 4);
	vec4 b5 = shape_vec(block + 5);
	vec4 b6 = shape_vec(block + 6);
	vec2 up = vec2(b3.x, b3.y);
	float level = up.x * o.x + up.y * o.y;
	float rising = up.x * u.x + up.y * u.y;
	vec2 best = vec2(within, -1.0);
	if (b3.z > 0.5 && abs(rising) > 1.0e-14) {
		best = inside_nearer(block, o, u, (b3.w - level) / rising, 0.0, best);
	}
	if (b4.w > 0.5 && abs(rising) > 1.0e-14) {
		best = inside_nearer(block, o, u, (b5.z - level) / rising, 1.0, best);
	}
	if (b3.z > 0.5 && b4.x > 0.0) {
		vec2 meets = cone_meets(o, u, up, vec2(b5.x, b5.y), b4.y, b4.z, 1.0 - (b3.w - level) / b4.x, rising / b4.x);
		best = inside_nearer(block, o, u, meets.x, 2.0, best);
		best = inside_nearer(block, o, u, meets.y, 2.0, best);
	}
	if (b4.w > 0.5 && b5.w > 0.0) {
		vec2 meets = cone_meets(o, u, up, vec2(b6.z, b6.w), b6.x, b6.y, 1.0 - (level - b5.z) / b5.w, -rising / b5.w);
		best = inside_nearer(block, o, u, meets.x, 3.0, best);
		best = inside_nearer(block, o, u, meets.y, 3.0, best);
	}
	return vec2(best.y < -0.5 ? -1.0 : best.x, best.y);
}

// the normal of a surface within a core, at a place on it
vec3 inside_normal(int block, int which, vec3 p) {
	vec4 b3 = shape_vec(block + 3);
	vec2 up = vec2(b3.x, b3.y);
	if (which < 2) {
		return vec3(up.x, up.y, 0.0);
	}
	vec4 b4 = shape_vec(block + 4);
	vec4 b5 = shape_vec(block + 5);
	vec4 b6 = shape_vec(block + 6);
	bool vortex = which == 2;
	vec2 middle = vortex ? vec2(b5.x, b5.y) : vec2(b6.z, b6.w);
	float across = vortex ? b4.y : b6.x;
	float deep = vortex ? b4.z : b6.y;
	float level = up.x * p.x + up.y * p.y;
	float q = ((p.x - middle.x) * up.y - (p.y - middle.y) * up.x) / across;
	float w = p.z / deep;
	float h = vortex ? 1.0 - (b3.w - level) / b4.x : 1.0 - (level - b5.z) / b5.w;
	float slope = vortex ? 1.0 / b4.x : -1.0 / b5.w;
	// the gradient of q^2 + w^2 - h^2
	return vec3(2.0 * q * up.y / across - 2.0 * h * slope * up.x, -2.0 * q * up.x / across - 2.0 * h * slope * up.y, 2.0 * w / deep);
}

bool patch_within(int at, vec3 p) {
	vec4 hr = shape_vec(at + 2);
	vec4 box = shape_vec(at + 3);
	if (!(p.x >= box.x - TRACE_SLACK && p.x <= box.y + TRACE_SLACK && p.y >= hr.x - TRACE_SLACK && p.y <= hr.y + TRACE_SLACK && p.z >= box.z - TRACE_SLACK && p.z <= box.w + TRACE_SLACK)) {
		return false;
	}
	bool torus = shape_vec(at + 1).w > 0.5;
	if (hr.z > 0.0 || hr.w < 1.0e29 || torus) {
		float rho = sqrt(p.x * p.x + p.z * p.z);
		if (rho < hr.z - TRACE_SLACK || rho > hr.w + TRACE_SLACK) {
			return false;
		}
		if (torus) {
			// on its own arc of the circle: how far round from the arc's start, in [0, 2 pi)
			vec4 ring = shape_vec(at + 4);
			float turn = 6.283185307179586;
			float d = atan(p.y - ring.y, rho - ring.x) - ring.w;
			d = d - turn * floor(d / turn);
			d = d > turn - TRACE_SLACK ? d - turn : d;
			return d <= shape_vec(at + 5).x - ring.w + TRACE_SLACK;
		}
	}
	return true;
}

// The real roots of x^4 + A x^3 + B x^2 + C x + D, in rising order, 1e30 for each it has not (Stage 33), in
// closed form (Ferrari's way): about the mean of its roots it is y^4 + p y^2 + q y + r, the product of
// y^2 + m y + k and y^2 - m y + l, where m^2 is a root of the cubic z^3 + 2 p z^2 + (p^2 - 4 r) z - q^2 (its
// largest, which is never below zero), k and l from p, q and m. Nothing is stepped or halved: a pixel's cost on
// the GPU is what the pixels run with it could do, and a search that runs until it is done there made five
// sixths of the pass's whole cost (measured). A quadratic's roots that just fail to be real are given as one
// (`rough`: how far below zero its discriminant may be, for the arithmetic's own error): what follows a root
// makes it exact, or finds it is none.
#ifndef TRACE_ROUGH
#define TRACE_ROUGH 1.0e-4
#endif
vec4 quartic_roots(float A, float B, float C, float D) {
	float A2 = A * A;
	float p = B - 0.375 * A2;
	float q = C - 0.5 * A * B + 0.125 * A2 * A;
	float r = D - 0.25 * A * C + 0.0625 * A2 * B - 0.01171875 * A2 * A2;
	float a2 = 2.0 * p;
	float a1 = p * p - 4.0 * r;
	float a0 = -q * q;
	// the cubic's largest root: about -a2 / 3 it is v^3 + P v + Q
	float P = a1 - a2 * a2 / 3.0;
	float Q = 2.0 * a2 * a2 * a2 / 27.0 - a2 * a1 / 3.0 + a0;
	float half_q = 0.5 * Q;
	float third_p = P / 3.0;
	float disc = half_q * half_q + third_p * third_p * third_p;
	float v = 0.0;
	if (disc > 0.0) {
		float cube = pow(abs(half_q) + sqrt(disc), 1.0 / 3.0);
		float first = half_q > 0.0 ? -cube : cube;
		v = cube > 0.0 ? first - third_p / first : 0.0;
	} else {
		float m = sqrt(max(-third_p, 0.0));
		v = m > 0.0 ? 2.0 * m * cos(acos(clamp(-half_q / (m * m * m), -1.0, 1.0)) / 3.0) : 0.0;
	}
	float z = v - a2 / 3.0;
	// (one of Newton's steps on the cubic itself: the closed form loses digits where its roots are close)
	float slope = (3.0 * z + 2.0 * a2) * z + a1;
	if (slope != 0.0) {
		z = z - (((z + a2) * z + a1) * z + a0) / slope;
	}
	z = max(z, 0.0);
	vec4 roots = vec4(1.0e30, 1.0e30, 1.0e30, 1.0e30);
	float scale = abs(p) + abs(z) + sqrt(abs(r));
	if (z > 1.0e-12 * scale && scale > 0.0) {
		float m = sqrt(z);
		float k = 0.5 * (p + z - q / m);
		float l = 0.5 * (p + z + q / m);
		float d1 = z - 4.0 * k;
		float d2 = z - 4.0 * l;
		float slack = TRACE_ROUGH * scale;
		if (d1 >= -slack) {
			// y^2 + m y + k (the pair without cancellation)
			float h = -0.5 * (m + sqrt(max(d1, 0.0)));
			roots.x = h;
			roots.y = h != 0.0 ? k / h : 0.0;
		}
		if (d2 >= -slack) {
			// y^2 - m y + l
			float h = 0.5 * (m + sqrt(max(d2, 0.0)));
			roots.z = h;
			roots.w = h != 0.0 ? l / h : 0.0;
		}
	} else {
		// no y to the first power: a quadratic in y^2
		float dw = p * p - 4.0 * r;
		if (dw >= -TRACE_ROUGH * (p * p + abs(r))) {
			float root = sqrt(max(dw, 0.0));
			float w0 = 0.5 * (-p - root);
			float w1 = 0.5 * (-p + root);
			if (w0 >= 0.0) {
				roots.x = -sqrt(w0);
				roots.y = sqrt(w0);
			}
			if (w1 >= 0.0) {
				roots.z = -sqrt(w1);
				roots.w = sqrt(w1);
			}
		}
	}
	// back from about their mean, and in order (unused ones stay last)
	float shift = -0.25 * A;
	roots = vec4(roots.x < 1.0e29 ? roots.x + shift : 1.0e30, roots.y < 1.0e29 ? roots.y + shift : 1.0e30, roots.z < 1.0e29 ? roots.z + shift : 1.0e30, roots.w < 1.0e29 ? roots.w + shift : 1.0e30);
	float lo1 = min(roots.x, roots.y);
	float hi1 = max(roots.x, roots.y);
	float lo2 = min(roots.z, roots.w);
	float hi2 = max(roots.z, roots.w);
	float mid1 = max(lo1, lo2);
	float mid2 = min(hi1, hi2);
	return vec4(min(lo1, lo2), min(mid1, mid2), max(mid1, mid2), max(hi1, hi2));
}

// A torus patch's nearest crossing after `after` and before `before`, within its bounds (Stage 33). Along the
// ray the torus is a quartic in the distance along it, taken from where the ray is nearest the axis (its roots
// lie about there: no cancellation from a far origin). Its roots in closed form; each that may lie in a span
// of the ray within the patch's rings and heights is then made exact by three of Newton's steps on the
// distance to the torus itself (the distance from the circle its tube is about, less the tube's radius: far
// better kept in single precision than the quartic), and is a crossing if that distance is then nothing.
float torus_meet(int at, vec3 o, vec3 u, float after, float before) {
#ifdef OPTICS_WORK_HOOK
	OPTICS_WORK_HOOK(3);
#endif
	vec4 hr = shape_vec(at + 2);
	vec4 ring = shape_vec(at + 4);
	float r0 = max(0.0, hr.z - TRACE_SLACK);
	float r1 = hr.w + TRACE_SLACK;
	float a = u.x * u.x + u.z * u.z;
	float b = 2.0 * (o.x * u.x + o.z * u.z);
	float c = o.x * o.x + o.z * o.z;
	vec4 span = vec4(1.0, 0.0, 1.0, 0.0); // two stretches of the ray, each empty
	if (a < 1.0e-18) {
		if (!(c < r0 * r0 || c > r1 * r1)) {
			span = vec4(-1.0e30, 1.0e30, 1.0, 0.0);
		}
	} else {
		float disc1 = b * b - 4.0 * a * (c - r1 * r1);
		if (disc1 > 0.0) {
			float s1 = sqrt(disc1);
			float disc0 = b * b - 4.0 * a * (c - r0 * r0);
			if (r0 <= 0.0 || disc0 <= 0.0) {
				span = vec4((-b - s1) / (2.0 * a), (-b + s1) / (2.0 * a), 1.0, 0.0);
			} else {
				float s0 = sqrt(disc0);
				span = vec4((-b - s1) / (2.0 * a), (-b - s0) / (2.0 * a), (-b + s0) / (2.0 * a), (-b + s1) / (2.0 * a));
			}
		}
	}
	float y0 = -1.0e30;
	float y1 = 1.0e30;
	if (abs(u.y) > 1.0e-18) {
		float ya = (hr.x - TRACE_SLACK - o.y) / u.y;
		float yb = (hr.y + TRACE_SLACK - o.y) / u.y;
		y0 = min(ya, yb);
		y1 = max(ya, yb);
	} else if (o.y < hr.x - TRACE_SLACK || o.y > hr.y + TRACE_SLACK) {
		return -1.0;
	}
	float lo0 = max(max(span.x, after), y0);
	float hi0 = min(min(span.y, before), y1);
	float lo1 = max(max(span.z, after), y0);
	float hi1 = min(min(span.w, before), y1);
	if (!(hi0 > lo0) && !(hi1 > lo1)) {
		return -1.0;
	}
#ifdef OPTICS_WORK_HOOK
	OPTICS_WORK_HOOK(4);
#endif
	// from where the ray is nearest the axis (straight along the axis: from level with the tube's circle)
	float tc = a > 1.0e-18 ? -0.5 * b / a : (abs(u.y) > 1.0e-18 ? (ring.y - o.y) / u.y : 0.0);
	vec3 pc = ray_point(o, u, tc);
	float dy = pc.y - ring.y;
	float m2 = ring.x * ring.x;
	float a1 = 2.0 * (pc.x * u.x + pc.z * u.z);
	float a0 = pc.x * pc.x + pc.z * pc.z;
	float q2 = a + u.y * u.y;
	float q1 = a1 + 2.0 * dy * u.y;
	float q0 = a0 + dy * dy + m2 - ring.z * ring.z;
	float c4 = q2 * q2;
	if (!(c4 > 0.0)) {
		return -1.0;
	}
	vec4 roots = quartic_roots(2.0 * q2 * q1 / c4, (q1 * q1 + 2.0 * q2 * q0 - 4.0 * m2 * a) / c4, (2.0 * q1 * q0 - 4.0 * m2 * a1) / c4, (q0 * q0 - 4.0 * m2 * a0) / c4);
	float near = 0.05 * (ring.x + ring.z); // how far a root in closed form may be from where it is
	for (int i = 0; i < 4; i++) {
		float s = i == 0 ? roots.x : (i == 1 ? roots.y : (i == 2 ? roots.z : roots.w));
		float rough = tc + s;
		if (s > 1.0e29 || !((rough > lo0 - near && rough < hi0 + near) || (rough > lo1 - near && rough < hi1 + near))) {
			continue;
		}
#ifdef OPTICS_WORK_HOOK
		OPTICS_WORK_HOOK(5);
#endif
		float off = 0.0;
		for (int j = 0; j < 4; j++) {
			float rho = sqrt(max((a * s + a1) * s + a0, 0.0));
			float rise = dy + u.y * s;
			float from = sqrt((rho - ring.x) * (rho - ring.x) + rise * rise);
			off = from - ring.z;
			if (j == 3 || !(rho > 0.0) || !(from > 0.0)) {
				break;
			}
			float slope = ((rho - ring.x) * (a * s + 0.5 * a1) / rho + rise * u.y) / from;
			if (abs(slope) < 1.0e-6) {
				break;
			}
			s = s - clamp(off / slope, -near, near);
		}
		float t = tc + s;
		if (abs(off) <= 8.0 * TRACE_SLACK && t > after && t < before && ((t >= lo0 && t <= hi0) || (t >= lo1 && t <= hi1)) && patch_within(at, ray_point(o, u, t))) {
			return t;
		}
	}
	return -1.0;
}

// what becomes of one ray in one shape: where it last was and which way it went on (cm, the shape's frame),
// what its surfaces passed, its path in the core, how far it went before it met the shape, its route
#ifndef OPTICS_THREAD
#define OPTICS_THREAD
#endif
// How far down the screen a place of the shape's own frame is, from the frame's origin (Stage 33; trace_body
// sets it): a ray keeps the rows of the screen between which it meets the shape's surfaces and what lies in it
// (Traced.span), so that a change of what lies in a body is known to pass a ray by.
OPTICS_THREAD vec2 trace_level = vec2(0.0, 0.0);

struct Traced {
	vec2 span;
	vec3 at;
	vec3 toward;
	float pass;
	float absorbed; // what its glass took in (Stage 46: glass is opaque in the long-wave)
	float turned; // what its surfaces turned away (pass + absorbed + turned = 1: O22)
	float chord;
	float first;
	uint route;
	int meetings;
	bool left;
	bool solid; // it ended in the sediment (Stage 30)
};

float trace_index(int medium, float n_glass, float n_core) {
	return medium == 1 ? n_glass : (medium == 2 ? n_core : 1.0);
}

// (`block`: the body's place in the shapes' head, if what lies in its core is given there; else -1, and its core
// is one thing, of the index n_core)
// (`alpha_glass`, `alpha_core`: the glass's absorption per cm, and the core's where the core is glass, a prism's;
// Stage 46)
// Fresnel's unpolarised reflectance onto a metal (Stage 78; Born and Wolf §13.2): from a clear medium of index n1 onto
// one of n2 + i k2, the light arriving at cos_i; complex arithmetic by hand, a vec2 a number (the truth's is the core's
// chem::optics::fresnel_reflectance, written apart)
vec2 cx_mul(vec2 a, vec2 b) {
	return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}
vec2 cx_div(vec2 a, vec2 b) {
	float d = b.x * b.x + b.y * b.y;
	return vec2((a.x * b.x + a.y * b.y) / d, (a.y * b.x - a.x * b.y) / d);
}
vec2 cx_root(vec2 a) {
	float m = sqrt(a.x * a.x + a.y * a.y);
	float re = sqrt(max(0.0, 0.5 * (m + a.x)));
	float im = sqrt(max(0.0, 0.5 * (m - a.x)));
	return vec2(re, a.y < 0.0 ? -im : im);
}
float metal_reflectance(float n1, float n2, float k2, float cos_i) {
	vec2 a = vec2(n1, 0.0);
	vec2 b = vec2(n2, k2);
	vec2 ci = vec2(cos_i, 0.0);
	vec2 ratio = cx_div(a, b);
	vec2 r2 = cx_mul(ratio, ratio);
	vec2 ct = cx_root(vec2(1.0 - r2.x * (1.0 - cos_i * cos_i), -r2.y * (1.0 - cos_i * cos_i)));
	vec2 a_ci = cx_mul(a, ci);
	vec2 b_ct = cx_mul(b, ct);
	vec2 b_ci = cx_mul(b, ci);
	vec2 a_ct = cx_mul(a, ct);
	vec2 rs = cx_div(vec2(a_ci.x - b_ct.x, a_ci.y - b_ct.y), vec2(a_ci.x + b_ct.x, a_ci.y + b_ct.y));
	vec2 rp = cx_div(vec2(b_ci.x - a_ct.x, b_ci.y - a_ct.y), vec2(b_ci.x + a_ct.x, b_ci.y + a_ct.y));
	return min(1.0, 0.5 * (rs.x * rs.x + rs.y * rs.y + rp.x * rp.x + rp.y * rp.y));
}

// A reflection is a branch (Stage 78, GAME_SPEC §12.3). A ray with a branch seed is turned back at a clear surface
// or goes through, chosen by the surface's reflectance R against a uniform drawn from its seed and the count of
// clear surfaces it has met (Russian roulette): its weight is unchanged either way, so the mean of many such rays
// is T of the light through and R of the light turned back. The seed 0 is the beam's own ray (its corners' and its
// middle's, kept and shared: Stage 33), which takes no branch: what its surfaces turn away is shown as the room.
#ifndef OPTICS_THREAD
#define OPTICS_THREAD
#endif
OPTICS_THREAD uint ray_seed = 0u;
OPTICS_THREAD int ray_events = 0;

// a uniform in [0, 1) from a seed and an event's count (a PCG hash's output, 24 bits)
float branch_draw(uint seed, int i) {
	uint h = seed ^ (uint(i) * 2654435769u);
	h = h * 747796405u + 2891336453u;
	h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
	h = (h >> 22u) ^ h;
	return float(h >> 8u) / 16777216.0;
}

// (`k_core`, Stage 78: the core's k, a metal's; nought for a clear core. A ray that comes to a core with k is turned
// back by metal_reflectance and the rest absorbed in its skin: it never goes in)
Traced trace_shape(int s, vec3 from, vec3 heading, float n_glass, float n_core, int block, float alpha_glass, float alpha_core, float k_core) {
	Traced r;
	vec3 o = from;
	vec3 u = heading;
	r.pass = 1.0;
	r.absorbed = 0.0;
	r.turned = 0.0;
	r.chord = 0.0;
	r.first = 0.0;
	r.route = uint(0);
	r.meetings = 0;
	r.left = true;
	r.solid = false;
	r.span = vec2(1.0e30, -1.0e30);
	int count = int(shape_vec(s).x);
	int here = 0;
	int turned = 0; // how often it has been turned back
	float reach = shape_vec(s).y; // how far from its axis the shape reaches
	for (int m = 0; m < TRACE_MOST; m++) {
		float best = 1.0e30;
#ifdef OPTICS_WORK_HOOK
		OPTICS_WORK_HOOK(1);
		if (m == 8) {
			OPTICS_WORK_HOOK(6);
		}
		if (m == TRACE_MOST - 1) {
			OPTICS_WORK_HOOK(7);
		}
#endif
		int hit = -1;
		// Where the ray can meet the shape (Stage 33): within its reach from the axis. The heights it has there, and
		// the nearest it comes to the axis: a surface wholly above or below those heights, or wholly nearer the
		// axis, is not tried. (A group of pixels on the GPU pays for every surface any of them tries.)
		float y_lo = -1.0e30;
		float y_hi = 1.0e30;
		float rho_least = 0.0;
		float ua = u.x * u.x + u.z * u.z;
		if (reach < 1.0e29 && ua > 1.0e-12) {
			float ub = o.x * u.x + o.z * u.z;
			float uc = o.x * o.x + o.z * o.z;
			float within = ub * ub - ua * (uc - reach * reach);
			if (!(within > 0.0)) {
				break; // it passes the shape by
			}
			float root = sqrt(within);
			float t_in = max((-ub - root) / ua, 0.0);
			float t_out = (-ub + root) / ua;
			if (!(t_out > 0.0)) {
				break; // the shape is behind it
			}
			y_lo = o.y + min(u.y * t_in, u.y * t_out) - 4.0 * TRACE_SLACK;
			y_hi = o.y + max(u.y * t_in, u.y * t_out) + 4.0 * TRACE_SLACK;
			rho_least = sqrt(max(uc - ub * ub / ua, 0.0)) - 4.0 * TRACE_SLACK;
		}
		for (int i = 0; i < 64; i++) {
			if (i >= count) {
				break;
			}
			int at = s + 1 + i * SHAPE_PATCH;
			vec4 hr = shape_vec(at + 2);
			if (hr.y < y_lo || hr.x > y_hi || hr.w < rho_least) {
				continue;
			}
			vec4 qa = shape_vec(at);
			vec4 ql = shape_vec(at + 1);
			if (ql.w > 0.5) {
				continue; // a torus: after the others, and only before the nearest of them
			}
			// f(o + t u) = a t^2 + b t + c
#ifdef OPTICS_WORK_HOOK
			OPTICS_WORK_HOOK(2);
#endif
			float a = qa.x * u.x * u.x + qa.y * u.y * u.y + qa.z * u.z * u.z;
			float b = 2.0 * (qa.x * o.x * u.x + qa.y * o.y * u.y + qa.z * o.z * u.z) + (ql.x * u.x + ql.y * u.y + ql.z * u.z);
			float c = qa.x * o.x * o.x + qa.y * o.y * o.y + qa.z * o.z * o.z + (ql.x * o.x + ql.y * o.y + ql.z * o.z) + qa.w;
			float root0 = -1.0;
			float root1 = -1.0;
			if (abs(a) < 1.0e-14) {
				if (abs(b) > 1.0e-14) {
					root0 = -c / b;
				}
			} else {
				float disc = b * b - 4.0 * a * c;
				if (disc >= 0.0) {
					// the stable pair: no cancellation in the smaller root
					float q = -0.5 * (b + (b < 0.0 ? -1.0 : 1.0) * sqrt(disc));
					root0 = q / a;
					if (q != 0.0) {
						root1 = c / q;
					}
				}
			}
			if (root0 > TRACE_NEAR && root0 < best && patch_within(at, ray_point(o, u, root0))) {
				best = root0;
				hit = i;
			}
			if (root1 > TRACE_NEAR && root1 < best && patch_within(at, ray_point(o, u, root1))) {
				best = root1;
				hit = i;
			}
		}
		for (int i = 0; i < 64; i++) {
			if (i >= count) {
				break;
			}
			int at = s + 1 + i * SHAPE_PATCH;
			vec4 hr = shape_vec(at + 2);
			if (hr.y < y_lo || hr.x > y_hi || hr.w < rho_least) {
				continue;
			}
			if (shape_vec(at + 1).w > 0.5) {
				float t = torus_meet(at, o, u, TRACE_NEAR, best);
				if (t > TRACE_NEAR && t < best) {
					best = t;
					hit = i;
				}
			}
		}
		if (block >= 0 && (here == 2 || here == 3)) {
			// within the core (Stage 30): the nearest place before the glass where what the ray is in changes
			vec2 crossing = inside_crossing(block, o, u, best);
			if (crossing.x > 0.0) {
				int which = int(crossing.y);
				if (here == 2) {
					r.chord = r.chord + crossing.x;
				}
				o = ray_point(o, u, crossing.x);
				r.meetings = r.meetings + 1;
				r.span = vec2(min(r.span.x, trace_level.x * o.x + trace_level.y * o.y), max(r.span.y, trace_level.x * o.x + trace_level.y * o.y));
				int beyond = inside_medium(block, ray_point(o, u, TRACE_NUDGE));
				if (beyond == 4) {
					r.solid = true;
					r.route = r.route * uint(31) + uint(200 + which);
					break;
				}
				vec3 cn = inside_normal(block, which, o);
				float cn_length = sqrt(dot3(cn, cn));
				if (!(cn_length > 0.0)) {
					continue;
				}
				cn = vec3(cn.x / cn_length, cn.y / cn_length, cn.z / cn_length);
				if (dot3(cn, u) > 0.0) {
					cn = vec3(-cn.x, -cn.y, -cn.z);
				}
				float c1 = trace_index(here, n_glass, n_core);
				float c2 = trace_index(beyond, n_glass, n_core);
				float ccos_i = -dot3(cn, u);
				if (beyond == 2 && k_core > 0.0) {
					// onto a metal (Stage 78): turned back by its reflectance, the rest absorbed
					float cr = metal_reflectance(c1, c2, k_core, ccos_i);
					r.absorbed = r.absorbed + r.pass * (1.0 - cr);
					r.pass = r.pass * cr;
					u = vec3(u.x + 2.0 * ccos_i * cn.x, u.y + 2.0 * ccos_i * cn.y, u.z + 2.0 * ccos_i * cn.z);
					r.route = r.route * uint(31) + uint(2 * (100 + which)) + uint(600);
					continue;
				}
				float ceta = c1 / c2;
				float csin2 = ceta * ceta * (1.0 - ccos_i * ccos_i);
				if (csin2 >= 1.0) {
					turned = turned + 1;
					if (turned > TRACE_TURNS) {
						break; // turned back once too often: it goes on as it is (below)
					}
					u = vec3(u.x + 2.0 * ccos_i * cn.x, u.y + 2.0 * ccos_i * cn.y, u.z + 2.0 * ccos_i * cn.z);
					r.route = r.route * uint(31) + uint(2 * (100 + which) + 1);
					continue;
				}
				float ccos_t = sqrt(1.0 - csin2);
				float crs = (c1 * ccos_i - c2 * ccos_t) / (c1 * ccos_i + c2 * ccos_t);
				float crp = (c1 * ccos_t - c2 * ccos_i) / (c1 * ccos_t + c2 * ccos_i);
				if (ray_seed != 0u) {
					// a branch (Stage 78): turned back, or through, its weight as it was
					bool back = branch_draw(ray_seed, ray_events) < 0.5 * (crs * crs + crp * crp);
					ray_events = ray_events + 1;
					if (back) {
						u = vec3(u.x + 2.0 * ccos_i * cn.x, u.y + 2.0 * ccos_i * cn.y, u.z + 2.0 * ccos_i * cn.z);
						r.route = r.route * uint(31) + uint(2 * (100 + which)) + uint(700);
						continue;
					}
				} else {
					r.turned = r.turned + r.pass * 0.5 * (crs * crs + crp * crp);
					r.pass = r.pass * (1.0 - 0.5 * (crs * crs + crp * crp));
				}
				float ck = ceta * ccos_i - ccos_t;
				u = vec3(ceta * u.x + ck * cn.x, ceta * u.y + ck * cn.y, ceta * u.z + ck * cn.z);
				here = beyond;
				r.route = r.route * uint(31) + uint(2 * (100 + which));
				continue;
			}
		}
		if (hit < 0) {
			break;
		}
		int at = s + 1 + hit * SHAPE_PATCH;
		vec4 qa = shape_vec(at);
		vec4 ql = shape_vec(at + 1);
		vec4 media = shape_vec(at + 5);
		if (here == 0) {
			r.first = r.first + best;
		}
		if (here == 2) {
			r.chord = r.chord + best;
		}
		// what the glass takes in along the way (Stage 46: nothing in the visible, where alpha is 0)
		float alpha = here == 1 ? alpha_glass : (here == 2 ? alpha_core : 0.0);
		float kept = exp(-alpha * best);
		r.absorbed = r.absorbed + r.pass * (1.0 - kept);
		r.pass = r.pass * kept;
		if (alpha > 0.0 && r.pass < 1.0e-9) {
			// taken in, all but nothing (glass in the long-wave): it goes no further, one way at every wavelength, and
			// brings nothing from beyond (its pass is nothing)
			r.absorbed = r.absorbed + r.pass;
			r.pass = 0.0;
			// (where it would land is no wavelength's: it goes on from where it met the shape, as it came, so the rays of a
			// stretch go one way however the glass's index swings across its band)
			r.route = r.route * uint(31) + uint(11);
			r.at = from;
			r.toward = heading;
			r.left = false;
			return r;
		}
		o = ray_point(o, u, best);
		r.meetings = r.meetings + 1;
		r.span = vec2(min(r.span.x, trace_level.x * o.x + trace_level.y * o.y), max(r.span.y, trace_level.x * o.x + trace_level.y * o.y));
		// the surface's normal, and which medium is beyond it
		vec3 n = vec3(2.0 * qa.x * o.x + ql.x, 2.0 * qa.y * o.y + ql.y, 2.0 * qa.z * o.z + ql.z);
		if (ql.w > 0.5) {
			vec4 ring = shape_vec(at + 4);
			float rho = sqrt(o.x * o.x + o.z * o.z);
			float k = rho > 0.0 ? 2.0 * (rho - ring.x) / rho : 0.0;
			n = vec3(k * o.x, 2.0 * (o.y - ring.y), k * o.z);
		}
		float n_length = sqrt(dot3(n, n));
		if (!(n_length > 0.0)) {
			continue;
		}
		n = vec3(n.x / n_length, n.y / n_length, n.z / n_length);
		bool rising = dot3(n, u) > 0.0; // from f < 0 to f > 0
		int there = rising ? int(media.z) : int(media.y);
		if (there == (here == 3 ? 2 : here)) {
			continue; // grazing, or a surface between two parts of one medium
		}
		if (block >= 0 && there == 2) {
			// into the core (Stage 30): its liquid, the air over it, or the sediment lying against the glass
			there = inside_medium(block, ray_point(o, u, TRACE_NUDGE));
			if (there == 4) {
				r.solid = true;
				r.route = r.route * uint(31) + uint(2 * (hit + 1)) + uint(300);
				break;
			}
		}
		if (rising) {
			n = vec3(-n.x, -n.y, -n.z); // against the ray
		}
		float n1 = trace_index(here, n_glass, n_core);
		float n2 = trace_index(there, n_glass, n_core);
		float cos_i = -dot3(n, u);
		if (there == 2 && k_core > 0.0) {
			// onto a metal (Stage 78): turned back by its reflectance, the rest absorbed
			float mr = metal_reflectance(n1, n2, k_core, cos_i);
			r.absorbed = r.absorbed + r.pass * (1.0 - mr);
			r.pass = r.pass * mr;
			u = vec3(u.x + 2.0 * cos_i * n.x, u.y + 2.0 * cos_i * n.y, u.z + 2.0 * cos_i * n.z);
			r.route = r.route * uint(31) + uint(2 * (hit + 1)) + uint(600);
			continue;
		}
		float eta = n1 / n2;
		float sin2 = eta * eta * (1.0 - cos_i * cos_i);
		if (sin2 >= 1.0) {
			// turned back, all of it
			turned = turned + 1;
			if (turned > TRACE_TURNS) {
				break; // once too often: it goes on as it is (below)
			}
			u = vec3(u.x + 2.0 * cos_i * n.x, u.y + 2.0 * cos_i * n.y, u.z + 2.0 * cos_i * n.z);
			r.route = r.route * uint(31) + uint(2 * (hit + 1) + 1);
			continue;
		}
		float cos_t = sqrt(1.0 - sin2);
		float rs = (n1 * cos_i - n2 * cos_t) / (n1 * cos_i + n2 * cos_t);
		float rp = (n1 * cos_t - n2 * cos_i) / (n1 * cos_t + n2 * cos_i);
		if (ray_seed != 0u) {
			// a branch (Stage 78): turned back, or through, its weight as it was
			bool back = branch_draw(ray_seed, ray_events) < 0.5 * (rs * rs + rp * rp);
			ray_events = ray_events + 1;
			if (back) {
				u = vec3(u.x + 2.0 * cos_i * n.x, u.y + 2.0 * cos_i * n.y, u.z + 2.0 * cos_i * n.z);
				r.route = r.route * uint(31) + uint(2 * (hit + 1)) + uint(700);
				continue;
			}
		} else {
			r.turned = r.turned + r.pass * 0.5 * (rs * rs + rp * rp);
			r.pass = r.pass * (1.0 - 0.5 * (rs * rs + rp * rp));
		}
		float k = eta * cos_i - cos_t;
		u = vec3(eta * u.x + k * n.x, eta * u.y + k * n.y, eta * u.z + k * n.z);
		here = there;
		r.route = r.route * uint(31) + uint(2 * (hit + 1));
	}
	r.at = o;
	r.toward = u;
	if (r.solid) {
		// it ended in the sediment: it reaches no wall, and what its surfaces passed is what lights the sediment
		r.left = false;
		return r;
	}
	if (here != 0) {
		// still inside, turned back more often than is allowed or after every meeting allowed: it goes straight
		// on from where it is, as if the rest of the body were not there, with what its surfaces have passed
		r.route = r.route * uint(31) + uint(7);
	}
	if (!(u.z > 0.05)) {
		r.route = r.route * uint(31) + uint(5);
	}
	return r;
}

// What one body does to a ray on its way through the one world of the screen's pixels (x across, y down, z
// away from the eye): the ray it leaves on, how far along the ray it was met, what its surfaces passed, its
// path in its liquid (cm). Its core: its liquid (0), the air over it (1), or glass all through (2).
struct BodyMet {
	vec2 span; // the rows of the screen between which it met a body that something lies in (none: x above y)
	bool met;
	float entry;
	float pass;
	float absorbed; // (Stage 46)
	float turned;
	float chord;
	bool left;
	bool solid; // it ended in the body's sediment (Stage 30)
	uint route;
	vec3 at;
	vec3 toward;
};

BodyMet trace_body(int body, int core, float nm, float z_axis, vec3 lp, vec3 lu) {
	BodyMet e;
	e.met = false;
	e.entry = 0.0;
	e.pass = 1.0;
	e.absorbed = 0.0;
	e.turned = 0.0;
	e.chord = 0.0;
	e.left = true;
	e.solid = false;
	e.route = uint(0);
	e.at = lp;
	e.toward = lu;
	e.span = vec2(1.0e30, -1.0e30);
	int block = body < 0 ? -1 : body * SHAPE_BLOCK;
	int s = body < 0 ? -1 : int(shape_vec(block).x);
	if (s < 0) {
		return e;
	}
	int at = HEADER_VECS + body * BODY_VECS;
	vec4 head = shape_vec(block);
	vec4 turn = shape_vec(block + 1);
	vec4 rect = shape_vec(block + 2); // where it stands upright
	vec4 form = scene_vec(at + 1);
	bool prism = form.x > 1.5;
	bool lies = head.z > 0.5;
	bool turned = head.w != 0.0;
	float cm = form.y;
	float axis = rect.x + rect.z * 0.5 + (prism ? scene_vec(at + 3).z : 0.0);
	int start = set_of(body) * ROWS_PER_SET * ROW;
	float n_glass = row_at(start + 4 * ROW, nm);
	float n_in = core == 0 || lies ? row_at(start + 3 * ROW, nm) : (core == 2 ? n_glass : 1.0);
	float alpha = row_at(start + 2 * ROW, nm); // its glass's absorption per cm (Stage 46)
	float k_in = (core == 0 || lies) && !prism ? row_at(start + LIQUID_K_ROW * ROW, nm) : 0.0; // its liquid's k (Stage 78)
	// The body's frame: cm from its axis, y up from its foot. Turned (Stage 30; upside down is half a turn), the
	// line is turned back about the pivot first, and the line it leaves on turned forward again.
	float c = turned ? turn.z : 1.0;
	float sn = turned ? turn.w : 0.0;
	vec2 pivot = turned ? vec2(turn.x, turn.y) : vec2(0.0, 0.0);
	float base = rect.y + rect.w;
	vec3 back_p = vec3(pivot.x + (lp.x - pivot.x) * c + (lp.y - pivot.y) * sn, pivot.y - (lp.x - pivot.x) * sn + (lp.y - pivot.y) * c, lp.z);
	vec3 u = vec3(lu.x * c + lu.y * sn, -(-lu.x * sn + lu.y * c), lu.z);
	vec3 p = vec3((back_p.x - axis) * cm, (base - back_p.y) * cm, (back_p.z - z_axis) * cm);
	// from just before the body, on the line: where the line is nearest its middle, less its reach (a far
	// start loses the surfaces' precision in single precision)
	float reach = 1.01 * sqrt(rect.z * rect.z * 0.25 + rect.w * rect.w * 0.25) * cm + 0.1 + (prism ? rect.z * cm : 0.0);
	float back = reach - dot3(vec3(-p.x, rect.w * 0.5 * cm - p.y, -p.z), u);
	vec3 o = vec3(p.x - back * u.x, p.y - back * u.y, p.z - back * u.z);
	trace_level = vec2(sn / cm, -c / cm);
	Traced l = trace_shape(s, o, u, n_glass, prism ? n_glass : n_in, lies ? block : -1, alpha, prism || core == 2 ? alpha : 0.0, k_in);
	if (l.meetings == 0) {
		return e;
	}
	e.met = true;
	if (lies) {
		float origin = pivot.y + (axis - pivot.x) * sn + (base - pivot.y) * c; // the frame's origin, down the screen
		e.span = vec2(origin + l.span.x, origin + l.span.y);
	}
	e.entry = (l.first - back) / cm;
	e.pass = l.pass;
	e.absorbed = l.absorbed;
	e.turned = l.turned;
	e.chord = (core == 0 || lies) && !prism ? l.chord : 0.0;
	e.left = l.left;
	e.solid = l.solid;
	e.route = l.route;
	vec2 out_p = vec2(axis + l.at.x / cm, base - l.at.y / cm);
	vec2 out_u = vec2(l.toward.x, -l.toward.y);
	e.at = vec3(pivot.x + (out_p.x - pivot.x) * c - (out_p.y - pivot.y) * sn, pivot.y + (out_p.x - pivot.x) * sn + (out_p.y - pivot.y) * c, z_axis + l.at.z / cm);
	e.toward = vec3(out_u.x * c - out_u.y * sn, out_u.x * sn + out_u.y * c, l.toward.z);
	return e;
}

// ---- what a ray meets (GAME_SPEC §12.3, Stage 29)

// A grating (a thin plate ruled with lines up and down, as wide as the gaps between them) sends a ray on in
// several ways at once, its orders: order m is turned across the lines by m times the wavelength over the
// ruling's pitch (the grating equation: the sine of its angle changes by that), and carries the share of the
// light the ruling's Fourier series gives it: a quarter in the order 0, 1 / (pi m)^2 in each odd order, nothing
// in the even ones; half the light falls on the lines themselves. A ray is followed in one way at a time:
// `ray_way` says which (0: order 0; 1, 2: orders +1 and -1; 3, 4: +3 and -3: those that can leave a visible
// ruling), and a pixel is the sum of its ways, each by its share of what the grating lets on (way_share); what
// it does not let on shows the room in front (GRATING_PASS, as a surface's pass: body_does).
#ifndef OPTICS_THREAD
#define OPTICS_THREAD
#endif
OPTICS_THREAD int ray_way = 0;
const int GRATING_WAYS = 5;
const float GRATING_PASS = 0.4751581; // 1/4 + 2 / pi^2 + 2 / (9 pi^2)

int way_order(int way) {
	return way == 0 ? 0 : (way == 1 ? 1 : (way == 2 ? -1 : (way == 3 ? 3 : -3)));
}

float way_share(int way) {
	return way == 0 ? 0.5261408 : (way < 3 ? 0.2132366 : 0.0236930);
}

// One ray of the room's light, from the eye straight in at p, followed through every layer, the nearest
// first: where it lands on the wall, its route (the same for rays that go the same way), and what it met on
// the way, four bodies at most, the nearest first: which body, whether it bent the ray, what its surfaces
// passed, the ray's path in its liquid (cm); and where the ray crossed each one's layer.
struct RayMet {
	float x;
	float y;
	uint route;
	int count;
	vec4 s0;
	vec4 s1;
	vec4 s2;
	vec4 s3;
	vec4 w01;
	vec4 w23;
	vec4 absorb; // what each one's glass took in (Stage 46)
	vec2 span; // the rows of the screen between which it met bodies that something lies in (Stage 33)
	// each one's own route through its surfaces (the first two that bent it): for holding a ray to the truth's
	uint t0;
	uint t1;
	int ways; // in how many ways it could go on from a grating it met (1: it met none)
};

RayMet met_put(RayMet r, int k, vec4 slot, vec2 where, float absorbed) {
	r.absorb = vec4(k == 0 ? absorbed : r.absorb.x, k == 1 ? absorbed : r.absorb.y, k == 2 ? absorbed : r.absorb.z, k == 3 ? absorbed : r.absorb.w);
	if (k == 0) {
		r.s0 = slot;
		r.w01 = vec4(where.x, where.y, r.w01.z, r.w01.w);
	} else if (k == 1) {
		r.s1 = slot;
		r.w01 = vec4(r.w01.x, r.w01.y, where.x, where.y);
	} else if (k == 2) {
		r.s2 = slot;
		r.w23 = vec4(where.x, where.y, r.w23.z, r.w23.w);
	} else if (k == 3) {
		r.s3 = slot;
		r.w23 = vec4(r.w23.x, r.w23.y, where.x, where.y);
	}
	return r;
}

vec4 met_slot(RayMet r, int k) {
	return k == 0 ? r.s0 : (k == 1 ? r.s1 : (k == 2 ? r.s2 : r.s3));
}

vec2 met_where(RayMet r, int k) {
	return k == 0 ? vec2(r.w01.x, r.w01.y) : (k == 1 ? vec2(r.w01.z, r.w01.w) : (k == 2 ? vec2(r.w23.x, r.w23.y) : vec2(r.w23.z, r.w23.w)));
}

bool trace_bends(int body) {
	int at = HEADER_VECS + body * BODY_VECS;
	float shape = scene_vec(at + 1).x;
	return scene_vec(at + 5).w > 0.5 && (shape < 0.5 || shape > 1.5) && shape < 2.5;
}

// what a body is in a row: its floor (a cylinder's: glass all through), its liquid, or the air over it
int trace_core(int body, float row) {
	int at = HEADER_VECS + body * BODY_VECS;
	vec4 rect = scene_vec(at);
	vec4 form = scene_vec(at + 1);
	float floor_px = scene_vec(at + 5).z / form.y;
	bool made = shape_vec(body * SHAPE_BLOCK).y > 0.5;
	return !made && floor_px > 0.0 && row >= rect.y + rect.w - floor_px ? 2 : (row >= form.z ? 0 : 1);
}

uint route_mix(uint h, uint v) {
	return (h ^ v) * 16777619u;
}

// The room (Stage 33): a cube about the screen's middle, `side` pixels on a side, its back face the wall. Where a
// ray from p along u comes to it: 0, the wall, off the cells along its edges (y and z: where, on the screen's
// own pixels); 1, another cell of another face; 2, a cell along one of the cube's twelve edges, which is black.
vec3 room_exit(vec3 p, vec3 u, float wall, float side, float cell) {
	float half_side = 0.5 * side;
	vec4 middle = scene_vec(4); // where it stands on the screen (Stage 42: the view moves it)
	vec3 lo = vec3(middle.x - half_side, middle.y - half_side, wall - side);
	vec3 hi = vec3(middle.x + half_side, middle.y + half_side, wall);
	float tx = abs(u.x) > 1.0e-9 ? ((u.x > 0.0 ? hi.x : lo.x) - p.x) / u.x : 1.0e30;
	float ty = abs(u.y) > 1.0e-9 ? ((u.y > 0.0 ? hi.y : lo.y) - p.y) / u.y : 1.0e30;
	float tz = abs(u.z) > 1.0e-9 ? ((u.z > 0.0 ? hi.z : lo.z) - p.z) / u.z : 1.0e30;
	float t = max(min(tx, min(ty, tz)), 0.0);
	vec3 h = vec3(p.x + t * u.x, p.y + t * u.y, p.z + t * u.z);
	float ex = min(h.x - lo.x, hi.x - h.x);
	float ey = min(h.y - lo.y, hi.y - h.y);
	float ez = min(h.z - lo.z, hi.z - h.z);
	bool by_z = tz <= tx && tz <= ty;
	float from_edge = by_z ? min(ex, ey) : (tx <= ty ? min(ey, ez) : min(ex, ez));
	if (from_edge < cell) {
		return vec3(2.0, 0.0, 0.0);
	}
	if (by_z && u.z > 0.0) {
		return vec3(0.0, h.x, h.y);
	}
	return vec3(1.0, 0.0, 0.0);
}

// (Stage 78) which face a ray from p along u comes to: 0 the front, 1 and 2 the left and right, 3 and 4 the top and
// bottom, -1 the wall; and where on it (pixels from its corner), as room_exit finds it
vec3 room_face(vec3 p, vec3 u, float wall, float side) {
	float half_side = 0.5 * side;
	vec4 middle = scene_vec(4);
	vec3 lo = vec3(middle.x - half_side, middle.y - half_side, wall - side);
	vec3 hi = vec3(middle.x + half_side, middle.y + half_side, wall);
	float tx = abs(u.x) > 1.0e-9 ? ((u.x > 0.0 ? hi.x : lo.x) - p.x) / u.x : 1.0e30;
	float ty = abs(u.y) > 1.0e-9 ? ((u.y > 0.0 ? hi.y : lo.y) - p.y) / u.y : 1.0e30;
	float tz = abs(u.z) > 1.0e-9 ? ((u.z > 0.0 ? hi.z : lo.z) - p.z) / u.z : 1.0e30;
	float t = max(min(tx, min(ty, tz)), 0.0);
	vec3 h = vec3(p.x + t * u.x, p.y + t * u.y, p.z + t * u.z);
	if (tz <= tx && tz <= ty) {
		// the front; or the wall (-1), whose edge cells are the table's
		return vec3(u.z > 0.0 ? -1.0 : 0.0, clamp(h.x - lo.x, 0.0, side), clamp(h.y - lo.y, 0.0, side));
	}
	if (tx <= ty) {
		return vec3(u.x < 0.0 ? 1.0 : 2.0, clamp(h.z - lo.z, 0.0, side), clamp(h.y - lo.y, 0.0, side));
	}
	return vec3(u.y < 0.0 ? 3.0 : 4.0, clamp(h.x - lo.x, 0.0, side), clamp(h.z - lo.z, 0.0, side));
}

RayMet ray_met(vec2 p, float nm) {
#ifdef OPTICS_RAY_HOOK
	OPTICS_RAY_HOOK();
#endif
	RayMet r;
	ray_events = 0; // (Stage 78: its clear surfaces counted from the eye)
	r.count = 0;
	r.s0 = vec4(-1.0, 0.0, 1.0, 0.0);
	r.s1 = r.s0;
	r.s2 = r.s0;
	r.s3 = r.s0;
	r.w01 = vec4(0.0);
	r.w23 = vec4(0.0);
	r.absorb = vec4(0.0);
	r.t0 = 0u;
	r.t1 = 0u;
	r.ways = 1;
	r.span = vec2(1.0e30, -1.0e30);
	int bent_count = 0;
	int count = int(scene_vec(0).x);
	float wall = scene_vec(2).x;
	float gap = scene_vec(3).x;
	vec3 lp = vec3(p.x, p.y, -(scene_vec(3).y + 1.0) * gap - 400.0);
	vec3 lu = vec3(0.0, 0.0, 1.0);
	uint h = 2166136261u;
	bool lands = true;
	bool cut_off = false; // an order that does not leave a grating: it goes nowhere
	int last = count - 1;
	r.x = p.x;
	r.y = p.y;
	// (Stage 61) whether the ray is seen through something in hand (its pixel within that thing's outline, or the ray
	// met it): from there on the panels are not in its way (it sees the room's wall, wherever it goes, as over the
	// bench)
	bool through_hand = hand_over(p);
	for (int guard = 0; guard < 16; guard++) {
		if (last < 0 || !lands) {
			break;
		}
		float layer = scene_vec(HEADER_VECS + last * BODY_VECS + 5).y;
		float z = -layer * gap;
		int first = last;
		for (int i = 0; i < MAX_BODIES; i++) {
			if (first > 0 && scene_vec(HEADER_VECS + (first - 1) * BODY_VECS + 5).y == layer) {
				first = first - 1;
			} else {
				break;
			}
		}
		vec2 where = vec2(lp.x + (z - lp.z) * lu.x / lu.z, lp.y + (z - lp.z) * lu.y / lu.z);
		// the layer's body that bends which the line meets first
		int hit = -1;
		BodyMet met = trace_body(-1, 0, nm, z, lp, lu);
		met.entry = 1.0e30;
		int met_core = 0;
		// (Stage 78) each body tried starts its clear surfaces' count where the layer does: the one met keeps its own
		int events_before = ray_events;
		int events_met = ray_events;
		for (int k = first; k <= last; k++) {
			vec4 rect = scene_vec(HEADER_VECS + k * BODY_VECS);
			if (trace_bends(k) && !hidden(k) && !(through_hand && in_panel(k)) && where.y >= rect.y && where.y <= rect.y + rect.w) {
				// (a ray whose line passes the body's axis further off than it reaches does not meet it)
				float off = abs((lp.x - (rect.x + rect.z * 0.5)) * lu.z - (lp.z - z) * lu.x) / sqrt(lu.x * lu.x + lu.z * lu.z);
				if (off > rect.z * 1.6 + 2.0) {
					continue;
				}
				int core = trace_core(k, where.y);
#ifdef OPTICS_WORK_HOOK
				OPTICS_WORK_HOOK(0);
#endif
				ray_events = events_before;
				BodyMet e = trace_body(k, core, nm, z, lp, lu);
				if (e.met && e.entry < met.entry) {
					met = e;
					hit = k;
					met_core = core;
					events_met = ray_events;
				}
			}
		}
		ray_events = events_met;
		if (hit >= 0) {
			r.span = vec2(min(r.span.x, met.span.x), max(r.span.y, met.span.y));
		}
		for (int k = last; k >= first; k--) {
			bool bent = k == hit;
			if (!bent) {
				vec4 rect = scene_vec(HEADER_VECS + k * BODY_VECS);
				if (trace_bends(k) || hidden(k) || (through_hand && in_panel(k)) || !(where.x >= rect.x - 1.0 && where.x <= rect.x + rect.z + 1.0 && where.y >= rect.y - 1.0 && where.y <= rect.y + rect.w + 1.0)) {
					continue;
				}
			}
			Layer l = layer_at(k, where, 1.0e-3, 1.0e-3);
			if (!bent && !(l.cover > 0.5)) {
				continue; // a thing the ray passes beside
			}
			through_hand = through_hand || layer > 0.5; // (it met what is in hand: Stage 61)
			if (bent && lies_in(k)) {
				// what lies in it is traced (Stage 30): its sediment is where a ray ends, not a heap drawn over it
				l.solid = met.solid ? 1.0 : 0.0;
				l.depth_cm = 1.0;
			}
			bool gone = bent && (!met.left || met.toward.z <= 0.05);
			// a grating: the ray goes on in its way's order, turned across the ruling; an order whose sine would
			// pass 1 does not leave the grating
			bool grating = !bent && scene_vec(HEADER_VECS + k * BODY_VECS + 1).x > 5.5;
			bool ended = false;
			if (grating) {
				r.ways = GRATING_WAYS;
				float across = lu.x + float(way_order(ray_way)) * nm / max(scene_vec(HEADER_VECS + k * BODY_VECS + 2).x, 1.0);
				float sine2 = across * across + lu.y * lu.y;
				lp = vec3(where.x, where.y, z);
				ended = sine2 >= 0.9975; // (as a ray that leaves a vessel nearly sideways does not reach the wall)
				if (!ended) {
					lu = vec3(across, lu.y, sqrt(1.0 - sine2));
				}
			}
			h = route_mix(h, uint(k * 8 + (bent ? 1 : 0) + (l.solid > 0.5 ? 2 : 0) + (l.depth_cm > 0.0 ? 4 : 0)));
			h = route_mix(h, bent ? met.route + (gone ? 2147483648u : 0u) : (grating ? uint(ray_way) + (ended ? 64u : 32u) : 0u));
			h = route_mix(h, uint(bent ? met_core : 0));
			if (r.count < 4) {
				// (2: a grating, which passes a share as a body that bends does, and holds no liquid)
				// (3: a body that bent it and in whose sediment it ended: lit by the room through the liquid, there and back)
				r = met_put(r, r.count, vec4(float(k), bent ? (met.solid ? 3.0 : 1.0) : (grating ? 2.0 : 0.0), bent ? met.pass : (grating ? (ended ? 0.0 : GRATING_PASS) : 1.0), bent ? (met.solid ? 2.0 * met.chord : met.chord) : 0.0), where, bent ? met.absorbed : 0.0);
				r.count = r.count + 1;
			}
			if (ended) {
				lands = false;
				cut_off = true;
				r.x = 100000.0;
				r.y = lp.y;
				break;
			}
			if (bent) {
				if (bent_count == 0) {
					r.t0 = met.route;
				} else if (bent_count == 1) {
					r.t1 = met.route;
				}
				bent_count = bent_count + 1;
				lp = met.at;
				lu = met.toward;
				if (gone) {
					lands = false;
					r.x = 100000.0; // (one place for every ray that never reaches the wall: it shows the room in front)
					r.y = lp.y;
					break;
				}
			}
		}
		last = first - 1;
	}
	float side = scene_vec(3).z;
	if (side > 0.0 && !cut_off) {
		// in a room: to whichever face it comes to; the wall where it reaches it, else far off to one side
		vec3 comes = room_exit(lp, lu, wall, side, scene_vec(3).w);
		vec3 face = faces_ruled() ? room_face(lp, lu, wall, side) : vec3(-1.0, 0.0, 0.0);
		if (comes.x < 0.5) {
			r.x = comes.y;
			r.y = comes.z;
		} else if (face.x >= 0.0) {
			// (Stage 78) a face, ruled, its edge cells with it: where on it
			r.x = FACE_X + face.x * FACE_STRIDE + face.y;
			r.y = FACE_Y + face.z;
			h = route_mix(h, uint(face.x) + 50u);
		} else {
			r.x = comes.x > 1.5 ? -100000.0 : 100000.0;
			r.y = lp.y;
			h = route_mix(h, uint(comes.x) + 40u);
		}
	} else if (lands) {
		r.x = lp.x + (wall - lp.z) * lu.x / lu.z;
		r.y = lp.y + (wall - lp.z) * lu.y / lu.z;
	}
	r.route = h;
	return r;
}

// ---- a pixel's rays (GAME_SPEC §12.3, Stage 29)
//
// What a band needs of a ray (where it lands, what passes, its chords) is smooth in wavelength along one
// route. So a pixel's rays are traced at four wavelengths, the nodes of a stretch of the spectrum (even in
// 1 / wavelength squared, as glass disperses), once; every band reads them, a cubic through the four. The
// rays are a part's four corners, where the wall is swept, and its middle, where what gets through is read:
// a beam. Where a part's rays do not go one way, or its middle ray does not land half way between its
// corners', the part is cut in four, twice at most; then cut where its rays part, each side its own beam;
// and a side whose rays still part along the spectrum is read at each node's own ray. Where the middle
// ray's way changes along the spectrum, the spectrum is cut there first.

// How far a part's middle ray may land from the mean of its corners' (its footprint is read through the
// middle ray too, so this bounds what is left over, not the whole of the bend): a share of a pixel of the wall,
// or of the footprint's own size where that is more than a pixel (near a rim one pixel of the screen sees
// hundreds of the wall's: what matters there is the share of the footprint that is misplaced)
#define BEAM_STRAIGHT 0.05
// How small a part whose rays part is made before it is cut where they do: 0, the pixel itself is cut; 1, its
// quarters; 2, their quarters.
#define BEAM_DEEPEST 2
// and how small a part whose rays part is made at least: the pixel itself is cut (measured over a bench of four
// vessels, a pixel then follows 51 rays on average, 420 at most; cut only as quarters of quarters, 195 and 5,700;
// O7, O8 and O11 hold either way, and O8 fails if a bent part is quartered only once)
#define BEAM_CUT_AT 0
// How many rays a pixel may follow before what is left of it is read as it lies: a part whose rays still part
// by its middle ray alone, at each node (a pixel on a rim holds many thin bands of rays that go different
// ways, and following each to its edges costs thousands of rays for a pixel that shows their blur)
#define BEAM_RAYS_MOST 100000
// How far from where its nodes' cubic puts it a pixel's middle ray may land, traced half way between them: the
// sweep's tolerance, of a pixel of the wall or of the footprint's size; and a hundredth of how far its
// landing sweeps along the stretch. Beyond it the stretch of the spectrum is halved. (0 or less: never, the
// whole spectrum by four nodes whatever they miss: what O10 rules out.)
#ifndef BEAM_BETWEEN
#define BEAM_BETWEEN 0.02
#endif

// Where a beam's rays all go one way (four pixels in five), one band in so many has a traced ray's correction in
// a frame (Stage 33), weighed as so many: the rays are the pass's cost, the corrections there are next to
// nothing, and the mean over the frames is the same.
#define CORRECTION_STRIDE 4
// (how many corrections the batch last read took: what it adds to a pixel's history is that many points, not its
// bands: a pixel is done when its corrections are enough, however thinly they were taken. Measured on the GPU:
// counted by its bands, a pixel whose rays part over a sliver of it was called done before a ray had found the
// sliver, six pixels more of 41,000 visibly off the truth.)
OPTICS_THREAD int corrections_taken = 0;

float place_nm(float g) {
	if (band_range == 1) {
		return 8000.0 / sqrt(1.0 - 0.6734693878 * clamp(g, 0.0, 1.0)); // (Stage 46)
	}
	return 380.0 / sqrt(1.0 - 0.7626561473 * clamp(g, 0.0, 1.0));
}

int beam_profile(float y) {
	return wall_profile(y);
}

Layer layer_none() {
	Layer l;
	l.body = 0;
	l.depth_cm = 0.0;
	l.haze = 0.0;
	l.solid = 0.0;
	l.wall = 0.0;
	l.glowing = 0.0;
	l.cover = 0.0;
	return l;
}

// A beam: its corners' landings at the four nodes (0 and 1 its top's, 2 and 3 its bottom's); its middle
// ray's, what that met (each body as it lies where the ray crossed it) and, for the first two that bent it,
// what their surfaces passed and its chord in their liquid, squared, at the nodes; each corner's route at
// the first node; how fast the middle's landing bends in the wavelength.
struct Beam {
	vec4 x0;
	vec4 y0;
	vec4 x1;
	vec4 y1;
	vec4 x2;
	vec4 y2;
	vec4 x3;
	vec4 y3;
	vec4 cx;
	vec4 cy;
	Layer l0;
	Layer l1;
	Layer l2;
	Layer l3;
	// (each body it met, as it was met: the body, how (met_put's kinds), and where the ray crossed its layer: what
	// the layers above are made from, so a beam that is kept is read with what the bodies hold now)
	vec4 m0;
	vec4 m1;
	vec4 m2;
	vec4 m3;
	int layers;
	vec4 bent;
	vec4 pass0;
	vec4 chord0;
	vec4 pass1;
	vec4 chord1;
	vec4 absorb0; // (Stage 46) what their glass took in
	vec4 absorb1;
	uint r0;
	uint r1;
	uint r2;
	uint r3;
	uint c0;
	uint c1;
	uint c2;
	uint c3;
	bool one; // every ray of it goes one way
	bool straight; // and its middle ray lands at the mean of its corners'
	float bend;
	float off; // how far its middle ray lands from the mean of its corners', at most (pixels)
	float reach; // how far apart its corners land, at most (pixels)
	// its middle ray traced between its nodes, a sixth of the way along its stretch of the spectrum (a whole
	// pixel's beam alone): where it lands and its route. If the cubic through the nodes does not put it there, four
	// nodes do not speak for the stretch, and the stretch is halved.
	float px;
	float py;
	uint pr;
	int form; // 0: a part or a side of one, four corners and a middle; 2: one ray
	int ways; // in how many ways its middle ray could go on from a grating (1: it met none)
	vec4 share; // how much of its part it is, at each node (a side of a cut: the cut moves with the wavelength)
	vec2 span; // the rows of the screen between which its rays met bodies that something lies in (Stage 33)
};

// ---- corners shared (GAME_SPEC §12.3, Stage 33)
//
// A pixel's beam is its middle ray and its four corners', and a corner is four pixels'. So the corners' rays are
// traced once, in a pass of their own over the lattice of pixel corners before the pixels' (corner_step), each
// kept until the shape key at its place moves; a pixel's beam is then nine rays, not twenty-one. The includer's
// corner_vec and corner_put hold four vec4s a corner, corner (X, Y) at
// 4 (1921 Y + X): where its ray lands at the four nodes (x, then y); its route at the first node in two halves,
// the second with 65536 added where the ray goes that way at every node, and the shape key it was traced
// for. Its rays are those of the whole spectrum's nodes and of a grating's first way: a beam of another
// stretch or way traces its own corners, as any beam does where there is no lattice.
OPTICS_THREAD bool lattice_on = false;

int corner_at(vec2 corner) {
	return 4 * (1921 * int(floor(corner.y + 0.5)) + int(floor(corner.x + 0.5)));
}

// one corner of the lattice: traced, if what stands at its place is not what it was traced for
void corner_step(vec2 corner) {
	int at = corner_at(corner);
	vec2 key = vec2(-1.0, 0.0); // nothing near: its ray goes straight
	vec2 content = vec2(0.0, 0.0);
	vec2 zone = vec2(1.0e30, -1.0e30);
	bool near = near_any(corner);
	if (near) {
		Path q = path_of(corner, 1.0);
		key = vec2(q.shape.x + 0.5, q.shape.y);
		content = q.content;
		zone = q.zone;
	}
	vec4 was = corner_vec(at + 2);
	if (was.z == key.x && was.w == key.y) {
		// where the bodies are as they were: what lies in them too, or changed only on rows its ray does not pass
		vec4 passed = corner_vec(at + 3);
		if (passed.z == content.x) {
			return;
		}
		if (passed.z == content.y && (passed.y < passed.x || passed.y + 0.05 < zone.x || passed.x - 0.05 > zone.y)) {
			corner_put(at + 3, vec4(passed.x, passed.y, content.x, 0.0));
			return;
		}
	}
	vec2 span = vec2(1.0e30, -1.0e30);
	vec4 xs = vec4(corner.x);
	vec4 ys = vec4(corner.y);
	uint route = 2166136261u; // (the route of a ray that meets nothing: ray_met's own beginning)
	bool same = true;
	if (near) {
		int way = ray_way;
		ray_way = 0;
		for (int w = 0; w < 4; w++) {
			RayMet m = ray_met(corner, place_nm(float(w) / 3.0));
			xs[w] = m.x;
			ys[w] = m.y;
			span = vec2(min(span.x, m.span.x), max(span.y, m.span.y));
			if (w == 0) {
				route = m.route;
			} else if (m.route != route) {
				same = false;
			}
		}
		ray_way = way;
	}
	corner_put(at, xs);
	corner_put(at + 1, ys);
	corner_put(at + 2, vec4(float(route & 65535u), float(route >> 16) + (same ? 65536.0 : 0.0), key.x, key.y));
	corner_put(at + 3, vec4(span.x, span.y, content.x, 0.0));
}

// a body the beam's middle ray met, as it lies where the ray crossed its layer (met: the body, how it was met,
// where)
Layer beam_layer(vec4 met) {
	Layer l = layer_at(int(met.x), vec2(met.z, met.w), 1.0e-3, 1.0e-3);
	if (met.y > 0.5 && lies_in(int(met.x))) {
		l.solid = met.y > 2.5 ? 1.0 : 0.0; // (what lies in it is traced, Stage 30)
	}
	return l;
}

// what a beam takes from its middle ray at the node w (`head`: its first, which says what the ray met)
Beam beam_took(Beam b, RayMet m, int w, bool head) {
	b.cx[w] = m.x;
	b.cy[w] = m.y;
	if (w == 0) {
		b.c0 = m.route;
	} else if (w == 1) {
		b.c1 = m.route;
	} else if (w == 2) {
		b.c2 = m.route;
	} else {
		b.c3 = m.route;
	}
	int records = 0;
	for (int k = 0; k < 4; k++) {
		if (k >= m.count) {
			break;
		}
		vec4 slot = met_slot(m, k);
		if (slot.y > 0.5 && records < 2) {
			if (records == 0) {
				b.pass0[w] = slot.z;
				b.chord0[w] = slot.w * slot.w;
				b.absorb0[w] = m.absorb[k];
			} else {
				b.pass1[w] = slot.z;
				b.chord1[w] = slot.w * slot.w;
				b.absorb1[w] = m.absorb[k];
			}
			if (head) {
				b.bent[k] = float(records);
			}
			records = records + 1;
		}
		if (head) {
			vec2 where = met_where(m, k);
			vec4 met = vec4(slot.x, slot.y, where.x, where.y);
			Layer l = beam_layer(met);
			if (k == 0) {
				b.l0 = l;
				b.m0 = met;
			} else if (k == 1) {
				b.l1 = l;
				b.m1 = met;
			} else if (k == 2) {
				b.l2 = l;
				b.m2 = met;
			} else {
				b.l3 = l;
				b.m3 = met;
			}
		}
	}
	if (head) {
		b.layers = m.count;
		b.ways = m.ways;
	}
	return b;
}

// kind 0: a part, corners p0 to p3 and its middle pc. 1: a side of a part that is cut (p0 to p3 the part's
// corners): `side` 0, across, from its left edge to where its top's and its bottom's rays part (at each node,
// the shares fa and fb of the way along them); 1, from there to its right edge; 2 and 3, up and down, above
// and below where its left's and its right's part. 2: one ray, such a side's middle, at the node `only`.
// Over the stretch of the spectrum from g_lo to g_hi (places: 0 at 380 nm, 1 at 780 nm).
Beam beam_trace(int kind, int side, vec2 p0, vec2 p1, vec2 p2, vec2 p3, vec2 pc, vec4 fa, vec4 fb, float g_lo, float g_hi, int only, bool between) {
	Beam b;
	b.x0 = vec4(0.0);
	b.y0 = vec4(0.0);
	b.x1 = vec4(0.0);
	b.y1 = vec4(0.0);
	b.x2 = vec4(0.0);
	b.y2 = vec4(0.0);
	b.x3 = vec4(0.0);
	b.y3 = vec4(0.0);
	b.cx = vec4(0.0);
	b.cy = vec4(0.0);
	b.l0 = layer_none();
	b.l1 = b.l0;
	b.l2 = b.l0;
	b.l3 = b.l0;
	b.m0 = vec4(0.0);
	b.m1 = vec4(0.0);
	b.m2 = vec4(0.0);
	b.m3 = vec4(0.0);
	b.layers = 0;
	b.bent = vec4(-1.0);
	b.pass0 = vec4(1.0);
	b.chord0 = vec4(0.0);
	b.pass1 = vec4(1.0);
	b.chord1 = vec4(0.0);
	b.absorb0 = vec4(0.0);
	b.absorb1 = vec4(0.0);
	b.r0 = 0u;
	b.r1 = 0u;
	b.r2 = 0u;
	b.r3 = 0u;
	b.c0 = 0u;
	b.c1 = 0u;
	b.c2 = 0u;
	b.c3 = 0u;
	b.one = true;
	b.straight = true;
	b.bend = 0.0;
	b.off = 0.0;
	b.reach = 0.0;
	b.px = 0.0;
	b.py = 0.0;
	b.pr = 0u;
	b.form = kind == 2 ? 2 : 0;
	b.ways = 1;
	b.share = vec4(1.0);
	b.span = vec2(1.0e30, -1.0e30);
	uint route = 0u;
	int points = kind == 2 ? 1 : 5;
	// (a whole pixel's beam over the whole spectrum, in a grating's first way: its corners are the lattice's)
	bool latticed = lattice_on && kind == 0 && g_lo <= 0.0 && g_hi >= 1.0 && ray_way == 0 && p1.x - p0.x == 1.0 && p2.y - p0.y == 1.0;
	for (int j = 0; j < 5; j++) {
		if (j >= points) {
			break;
		}
		for (int w = 0; w < 5; w++) {
			if ((kind == 2 && w != only) || (w == 4 && !(between && j == 0))) {
				continue;
			}
			if (w == 4) {
				// the middle ray between its nodes: a sixth of the way along, where a cubic through the four is
				// furthest from what it is drawn through
				RayMet half_way = ray_met(pc, place_nm(mix(g_lo, g_hi, 1.0 / 6.0)));
				b.px = half_way.x;
				b.py = half_way.y;
				b.pr = half_way.route;
				b.span = vec2(min(b.span.x, half_way.span.x), max(b.span.y, half_way.span.y));
				continue;
			}
			vec2 p = j == 0 ? pc : (j == 1 ? p0 : (j == 2 ? p1 : (j == 3 ? p2 : p3)));
			if (kind != 0) {
				// the side's own corners at this node, and its middle the mean of them
				vec2 c0 = p0;
				vec2 c1 = p1;
				vec2 c2 = p2;
				vec2 c3 = p3;
				if (side == 0) {
					c1 = vec2(mix(p0.x, p1.x, fa[w]), mix(p0.y, p1.y, fa[w]));
					c3 = vec2(mix(p2.x, p3.x, fb[w]), mix(p2.y, p3.y, fb[w]));
				} else if (side == 1) {
					c0 = vec2(mix(p0.x, p1.x, fa[w]), mix(p0.y, p1.y, fa[w]));
					c2 = vec2(mix(p2.x, p3.x, fb[w]), mix(p2.y, p3.y, fb[w]));
				} else if (side == 2) {
					c2 = vec2(mix(p0.x, p2.x, fa[w]), mix(p0.y, p2.y, fa[w]));
					c3 = vec2(mix(p1.x, p3.x, fb[w]), mix(p1.y, p3.y, fb[w]));
				} else if (side == 3) {
					c0 = vec2(mix(p0.x, p2.x, fa[w]), mix(p0.y, p2.y, fa[w]));
					c1 = vec2(mix(p1.x, p3.x, fb[w]), mix(p1.y, p3.y, fb[w]));
				}
				p = j == 0 ? vec2(0.25 * (c0.x + c1.x + c2.x + c3.x), 0.25 * (c0.y + c1.y + c2.y + c3.y)) : (j == 1 ? c0 : (j == 2 ? c1 : (j == 3 ? c2 : c3)));
			}
			RayMet m;
			if (latticed && j > 0) {
				int at = corner_at(p);
				vec4 kept_route = corner_vec(at + 2);
				bool same = kept_route.y >= 65536.0;
				uint first = uint(kept_route.x) + (uint(kept_route.y - (same ? 65536.0 : 0.0)) << 16);
				vec4 passed = corner_vec(at + 3);
				m.span = vec2(passed.x, passed.y);
				m.x = corner_vec(at)[w];
				m.y = corner_vec(at + 1)[w];
				m.route = same || w == 0 ? first : first ^ 1u; // (at a later node it goes another way: which, no one asks)
			} else {
				m = ray_met(p, place_nm(mix(g_lo, g_hi, float(w) / 3.0)));
			}
			b.span = vec2(min(b.span.x, m.span.x), max(b.span.y, m.span.y));
			bool head = j == 0 && (kind == 2 || w == 0);
			if (head) {
				route = m.route;
			} else if (m.route != route) {
				b.one = false;
			}
			if (j == 0) {
				b = beam_took(b, m, w, head);
			} else if (j == 1) {
				b.x0[w] = m.x;
				b.y0[w] = m.y;
				if (w == 0) {
					b.r0 = m.route;
				}
			} else if (j == 2) {
				b.x1[w] = m.x;
				b.y1[w] = m.y;
				if (w == 0) {
					b.r1 = m.route;
				}
			} else if (j == 3) {
				b.x2[w] = m.x;
				b.y2[w] = m.y;
				if (w == 0) {
					b.r2 = m.route;
				}
			} else {
				b.x3[w] = m.x;
				b.y3[w] = m.y;
				if (w == 0) {
					b.r3 = m.route;
				}
			}
		}
	}
	if (kind == 2) {
		// one ray: the same at every node, and its corners its own
		b.cx = vec4(b.cx[only]);
		b.cy = vec4(b.cy[only]);
		b.pass0 = vec4(b.pass0[only]);
		b.chord0 = vec4(b.chord0[only]);
		b.pass1 = vec4(b.pass1[only]);
		b.chord1 = vec4(b.chord1[only]);
		b.absorb0 = vec4(b.absorb0[only]);
		b.absorb1 = vec4(b.absorb1[only]);
		b.x0 = b.cx;
		b.y0 = b.cy;
		b.x1 = b.cx;
		b.y1 = b.cy;
		b.x2 = b.cx;
		b.y2 = b.cy;
		b.x3 = b.cx;
		b.y3 = b.cy;
		b.one = true;
	}
	for (int w = 0; w < 4; w++) {
		float mx = 0.25 * (b.x0[w] + b.x1[w] + b.x2[w] + b.x3[w]);
		float my = 0.25 * (b.y0[w] + b.y1[w] + b.y2[w] + b.y3[w]);
		float reach = max(max(max(abs(b.x1[w] - b.x0[w]), abs(b.x3[w] - b.x2[w])), max(abs(b.x2[w] - b.x0[w]), abs(b.x3[w] - b.x1[w]))),
				max(max(abs(b.y1[w] - b.y0[w]), abs(b.y3[w] - b.y2[w])), max(abs(b.y2[w] - b.y0[w]), abs(b.y3[w] - b.y1[w]))));
		float off = max(abs(b.cx[w] - mx), abs(b.cy[w] - my)) / max(reach, 1.0);
		b.off = max(b.off, off);
		b.reach = max(b.reach, reach);
		if (off > BEAM_STRAIGHT) {
			b.straight = false;
		}
	}
	if (b.one && kind != 2) {
		// how fast its landing bends in the wavelength, at most (pixels a nanometre squared), from its nodes
		float n0 = place_nm(g_lo);
		float n1 = place_nm(mix(g_lo, g_hi, 1.0 / 3.0));
		float n2 = place_nm(mix(g_lo, g_hi, 2.0 / 3.0));
		float n3 = place_nm(g_hi);
		if (n3 - n0 > 1.0e-6) {
			float ax = ((b.cx.z - b.cx.y) / (n2 - n1) - (b.cx.y - b.cx.x) / (n1 - n0)) / (n2 - n0);
			float bx = ((b.cx.w - b.cx.z) / (n3 - n2) - (b.cx.z - b.cx.y) / (n2 - n1)) / (n3 - n1);
			float ay = ((b.cy.z - b.cy.y) / (n2 - n1) - (b.cy.y - b.cy.x) / (n1 - n0)) / (n2 - n0);
			float by = ((b.cy.w - b.cy.z) / (n3 - n2) - (b.cy.z - b.cy.y) / (n2 - n1)) / (n3 - n1);
			b.bend = 2.0 * max(max(abs(ax), abs(bx)), max(abs(ay), abs(by)));
		}
	}
	return b;
}

// what the bodies the beam's middle ray met do to the wall's light at nm: a times it plus b (x, y); wp: the
// nodes' weights there
// The nodes' weights at g of the way through a beam's stretch of the spectrum. A cubic through the four, where
// its middle ray goes one way at all of them. Where it does not (it reaches the wall at some wavelengths and is
// turned back at others) nothing lies between two nodes: each wavelength is read at the node nearest it, that
// node's own ray (Stage 33). A cubic through a landing on the wall and the place a ray that never reaches it is
// put sweeps the whole wall between, and such a pixel settled on a colour that was not its rays' mean.
bool beam_nodes_one_way(Beam b) {
	return b.c0 == b.c1 && b.c1 == b.c2 && b.c2 == b.c3;
}

// whether the two nodes at the ends of a piece of the stretch (0, 1 or 2) go one way
bool beam_piece_one_way(Beam b, float piece) {
	return piece < 0.5 ? b.c0 == b.c1 : (piece < 1.5 ? b.c1 == b.c2 : b.c2 == b.c3);
}

vec4 beam_weights(Beam b, float g) {
	if (beam_nodes_one_way(b)) {
		return thirds_weights(g);
	}
	// between two nodes that go one way, straight from the one to the other (the landing sweeps there, and a
	// slit's image between them is in the sweep); between two that part, the nearer node
	float at = clamp(3.0 * g, 0.0, 3.0);
	float piece = min(floor(at), 2.0);
	float t = at - piece;
	if (!beam_piece_one_way(b, piece)) {
		t = t < 0.5 ? 0.0 : 1.0;
	}
	return vec4(piece < 0.5 ? 1.0 - t : 0.0, piece < 0.5 ? t : (piece < 1.5 ? 1.0 - t : 0.0), piece < 0.5 ? 0.0 : (piece < 1.5 ? t : 1.0 - t), piece > 1.5 ? t : 0.0);
}

// whether the places ga and gb of a beam's stretch lie between the same two nodes, which go one way: the
// landing sweeps straight from the one place to the other
bool beam_sweeps(Beam b, float ga, float gb) {
	float pa = min(floor(clamp(3.0 * ga, 0.0, 3.0)), 2.0);
	float pb = min(floor(clamp(3.0 * gb, 0.0, 3.0)), 2.0);
	return pa == pb && beam_piece_one_way(b, pa);
}

vec2 beam_shade(Beam b, vec4 wp, float nm) {
	float room = room_front();
	float a = 1.0;
	float sum = 0.0;
	for (int k = 0; k < 4; k++) {
		if (k >= b.layers) {
			break;
		}
		Layer l = k == 0 ? b.l0 : (k == 1 ? b.l1 : (k == 2 ? b.l2 : b.l3));
		int body = body_of(l.body);
		int start = set_in(l.body);
		float record = b.bent[k];
		float pass = 1.0;
		float absorbed = 0.0;
		float depth_cm = l.depth_cm + l.glowing;
		if (record > -0.5) {
			pass = clamp(record < 0.5 ? dot(b.pass0, wp) : dot(b.pass1, wp), 0.0, 1.0);
			absorbed = clamp(record < 0.5 ? dot(b.absorb0, wp) : dot(b.absorb1, wp), 0.0, 1.0 - pass);
			depth_cm = sqrt(max(record < 0.5 ? dot(b.chord0, wp) : dot(b.chord1, wp), 0.0));
		}
		vec4 met = k == 0 ? b.m0 : (k == 1 ? b.m1 : (k == 2 ? b.m2 : b.m3));
		if (record > -0.5) {
			l.haze = haze_along(body, depth_cm, met.w);
		}
		float glow = glow_of(body, start, vec2(met.z, met.w), 1.0e-3, 1.0e-3, nm, l.solid);
		vec2 does = body_does(l, row_at(start + ROW, nm), haze_colour(body, start, met.w, nm), glow, row_at(start + 2 * ROW, nm), liquid_at(body, start, nm, met.w) * depth_cm, pass, absorbed, room, heat_of(body, start, vec2(met.z, met.w), nm));
		sum = sum + a * does.y;
		a = a * does.x;
	}
	return vec2(a, sum);
}

// The wall's radiance over a box of it (pixels), where the wall is one there (Stage 33): one cell of its table
// across, on rows that are alike in that cell. -1 where it is not, or the box is eight rows tall or more.
float wall_one(float x_lo, float x_hi, float y_lo, float y_hi) {
	if (band_range == 1) {
		return 1.0;
	}
	if (!(y_hi - y_lo < 7.0) || x_lo >= 0.5 * FACE_X) {
		return -1.0; // (or landing nowhere; or on a face of the room, Stage 78: read as it is ruled)
	}
	int first = int(floor(y_lo));
	int last = int(floor(y_hi));
	int profile = beam_profile(y_lo);
	WallAt w = wall_at(profile, x_lo);
	if (!(x_hi < w.right)) {
		return -1.0;
	}
	int m = int(wall_of(0).x);
	for (int r = 1; r < 8; r++) {
		if (first + r > last) {
			break;
		}
		int other = beam_profile(float(first + r) + 0.5);
		if (other != profile && wall_of(WALL_BREAK_BASE + m + other * (m + 1) + w.cell).x != w.radiance) {
			return -1.0;
		}
	}
	return w.radiance;
}

// The wall over a beam's footprint as a step of a band is crossed: its corners land at xa (the step's
// beginning) and xb (its end), its top at `top` and its bottom at `bottom` (rows of the wall); `taper`: 1 where
// the piece narrows to a point at its bottom, -1 at its top, 0 where it does not (a quad). Row by row of
// the wall it crosses, each by its share of the footprint's height, the sweep across read exactly.
float beam_wall(float top, float bottom, vec4 xa, vec4 xb, float taper) {
	float lo = min(top, bottom);
	float hi = max(top, bottom);
	if (hi - lo < 1.0e-6) {
		return swept_wall(beam_profile(top), 0.5 * (xa.x + xa.z), 0.5 * (xa.y + xa.w), 0.5 * (xb.x + xb.z), 0.5 * (xb.y + xb.w));
	}
	int first = int(floor(lo));
	int rows = int(floor(hi)) - first + 1;
	bool whole = rows <= 8;
	int n = whole ? rows : 8;
	float sum = 0.0;
	for (int i = 0; i < 8; i++) {
		if (i >= n) {
			break;
		}
		float ya = whole ? max(lo, float(first + i)) : mix(lo, hi, float(i) / 8.0);
		float yb = whole ? min(hi, float(first + i + 1)) : mix(lo, hi, float(i + 1) / 8.0);
		if (!(yb > ya)) {
			continue;
		}
		float ym = 0.5 * (ya + yb);
		float f = (ym - top) / (bottom - top);
		// (a piece that narrows to a point is a triangle of the pixel: its rows weigh by their width there,
		// which falls straight to nothing at the point. Stage 35: each row weighed alike, a thin line under the
		// point's end of a piece was counted for more than its share of the pixel, and a pixel's mean was not its
		// rays' mean by a part in fifteen of such a line.)
		float weight = taper > 0.5 ? 2.0 * (1.0 - f) : (taper < -0.5 ? 2.0 * f : 1.0);
		int piece = lo >= 0.5 * FACE_Y ? face_profile(ya, yb) : beam_profile(ym); // (a face's: the piece's own lines across, Stage 78)
		sum = sum + (yb - ya) * weight * swept_wall(piece, mix(xa.x, xa.z, f), mix(xa.y, xa.w, f), mix(xb.x, xb.z, f), mix(xb.y, xb.w, f));
	}
	return sum / (hi - lo);
}

// the four nodes' weights at g of the way through their stretch, straight from each node to the next
vec4 between_weights(float g) {
	float s = clamp(3.0 * g, 0.0, 3.0);
	return vec4(max(1.0 - s, 0.0), max(1.0 - abs(s - 1.0), 0.0), max(1.0 - abs(s - 2.0), 0.0), max(s - 2.0, 0.0));
}

// The wall over a beam's whole footprint as a step of a band is crossed (wa, wb: the nodes' weights at the
// step's two ends; wp: at its point). A part's footprint is not its four corners' alone: its middle ray lands
// where it lands, off the mean of theirs where the glass magnifies unevenly. So the footprint is the four
// triangles from its edges to its middle ray, each a quarter of the part (the two beside it cut at the
// middle's own height, into pieces by their share of the edge): straight from each ray to the next, through
// all five.
// (Stage 33) A pixel shows its fixed estimate and the mean of what its batches found beyond it, so it comes to
// the batches' own mean whatever the fixed estimate is: the estimate need only be near, and cheap. It reads the
// footprint as its corners' quad, one piece; the batches read it through all five rays, six pieces. (Measured:
// with every ray traced, the footprint's pieces were all that kept a pour from the display's rate.)
OPTICS_THREAD bool fan_quad = false;

float beam_fan(Beam b, vec4 wa, vec4 wb, vec4 wp) {
	float ac = dot(b.cx, wa);
	float bc = dot(b.cx, wb);
	float yc = dot(b.cy, wp);
	if (b.form == 2) {
		return beam_wall(yc, yc, vec4(ac), vec4(bc), 0.0);
	}
	float a0 = dot(b.x0, wa);
	float a1 = dot(b.x1, wa);
	float b0 = dot(b.x0, wb);
	float b1 = dot(b.x1, wb);
	float y0 = dot(b.y0, wp);
	float y1 = dot(b.y1, wp);
	float a2 = dot(b.x2, wa);
	float a3 = dot(b.x3, wa);
	float b2 = dot(b.x2, wb);
	float b3 = dot(b.x3, wb);
	float y2 = dot(b.y2, wp);
	float y3 = dot(b.y3, wp);
	// where the wall is one under the whole of the step's footprint, it is that (Stage 33: most steps of most
	// beams, even of those that cross an edge of the wall's drawing somewhere in the spectrum)
	float one = wall_one(min(min(min(a0, a1), min(a2, a3)), min(min(b0, b1), min(min(b2, b3), min(ac, bc)))) - 1.0e-3, max(max(max(a0, a1), max(a2, a3)), max(max(b0, b1), max(max(b2, b3), max(ac, bc)))) + 1.0e-3,
			min(min(y0, y1), min(min(y2, y3), yc)) - 1.0e-3, max(max(y0, y1), max(max(y2, y3), yc)) + 1.0e-3);
	if (one >= 0.0) {
		return one;
	}
	if (fan_quad) {
		// the fixed estimate's footprint (Stage 33): its four corners' alone, from its top edge to its bottom
		// one. (Stage 35: a beam whose middle ray lands near the mean of its corners' was read so in its batches
		// too; near is not at, and a thin line's light through two vessels was 1 to 6% out, where O8 asks 1.)
		return beam_wall(0.5 * (y0 + y1), 0.5 * (y2 + y3), vec4(a0, a1, a2, a3), vec4(b0, b1, b2, b3), 0.0);
	}
	// above the middle, and below it
	float wall = 0.25 * (beam_wall(0.5 * (y0 + y1), yc, vec4(a0, a1, ac, ac), vec4(b0, b1, bc, bc), 1.0) + beam_wall(yc, 0.5 * (y2 + y3), vec4(ac, ac, a2, a3), vec4(bc, bc, b2, b3), -1.0));
	// to its left: from the left edge to the middle, above the middle's height and below
	float f = abs(y2 - y0) > 1.0e-9 ? clamp((yc - y0) / (y2 - y0), 0.0, 1.0) : 0.5;
	float ym = mix(y0, y2, f);
	float ae = mix(a0, a2, f);
	float be = mix(b0, b2, f);
	wall = wall + 0.25 * (f * beam_wall(y0, ym, vec4(a0, a0, ae, ac), vec4(b0, b0, be, bc), -1.0) + (1.0 - f) * beam_wall(ym, y2, vec4(ae, ac, a2, a2), vec4(be, bc, b2, b2), 1.0));
	// and to its right
	f = abs(y3 - y1) > 1.0e-9 ? clamp((yc - y1) / (y3 - y1), 0.0, 1.0) : 0.5;
	ym = mix(y1, y3, f);
	ae = mix(a1, a3, f);
	be = mix(b1, b3, f);
	return wall + 0.25 * (f * beam_wall(y1, ym, vec4(a1, a1, ac, ae), vec4(b1, b1, bc, be), -1.0) + (1.0 - f) * beam_wall(ym, y3, vec4(ac, ae, a3, a3), vec4(bc, be, b3, b3), 1.0));
}

// (lo, hi) widened to hold the four of v
vec2 reach_of(vec2 a, vec4 v) {
	return vec2(min(a.x, min(min(v.x, v.y), min(v.z, v.w))), max(a.y, max(max(v.x, v.y), max(v.z, v.w))));
}

// The wall's radiance under a beam's whole footprint, where the wall is one there at every wavelength of the
// beam's stretch (Stage 33); -1 where it is not. Most of a wall is plain, and there the footprint's integral
// (beam_fan: six pieces, each row by row, at every step of every band) is that radiance, whatever the sweep:
// the beam's rays all land in one cell of the wall's table, on rows that are alike there. The footprint between
// the nodes is a cubic through them, which passes up to a third of their range beyond them.
float beam_flat(Beam b) {
	vec2 xs = reach_of(vec2(1.0e30, -1.0e30), b.cx);
	vec2 ys = reach_of(vec2(1.0e30, -1.0e30), b.cy);
	if (b.form != 2) {
		xs = reach_of(reach_of(reach_of(reach_of(xs, b.x0), b.x1), b.x2), b.x3);
		ys = reach_of(reach_of(reach_of(reach_of(ys, b.y0), b.y1), b.y2), b.y3);
	}
	float beyond_x = 0.32 * (xs.y - xs.x) + 1.0e-3;
	float beyond_y = 0.32 * (ys.y - ys.x) + 1.0e-3;
	return wall_one(xs.x - beyond_x, xs.y + beyond_x, ys.x - beyond_y, ys.y + beyond_y);
}

// A band of the light through a beam, from nm0 to nm1, its point t of the way through (`nm_point`, if it is
// read in one step): swept in straight steps, as many as keep the sweep within its tolerance of the
// cubic's landing, each weighed by its share of the band's energy.
float beam_band(Beam b, float even, float g_lo, float g_hi, float nm0, float nm1, float t, float nm_point, float nm_share) {
	float span = max(g_hi - g_lo, 1.0e-9);
	bool one_way = beam_nodes_one_way(b);
	int steps = nm1 != nm0 && one_way ? map_steps(b.bend, abs(nm1 - nm0)) : 1; // (a band of nodes that part is read at its point's node)
	float total = 0.0;
	float weights = 0.0;
	float begin = nm0;
	float u_begin = steps > 1 ? share_of(nm0) : 0.0;
	for (int s = 1; s <= 16; s++) {
		if (s > steps) {
			break;
		}
		float end = mix(nm0, nm1, float(s) / float(steps));
		float nm = steps > 1 ? mix(begin, end, t) : nm_point;
		float u_end = steps > 1 ? share_of(end) : 1.0;
		float weight = max(u_end - u_begin, 0.0);
		vec4 wp = beam_weights(b, (map_place(nm) - g_lo) / span);
		vec4 wa = wp;
		vec4 wb = wp;
		if (one_way) {
			wa = thirds_weights((map_place(begin) - g_lo) / span);
			wb = thirds_weights((map_place(end) - g_lo) / span);
		} else if (beam_sweeps(b, (map_place(begin) - g_lo) / span, (map_place(end) - g_lo) / span)) {
			wa = beam_weights(b, (map_place(begin) - g_lo) / span);
			wb = beam_weights(b, (map_place(end) - g_lo) / span);
		}
		float wall = even;
		if (even < 0.0) {
			wall = beam_fan(b, wa, wb, wp);
		}
		vec2 ab = beam_shade(b, wp, nm);
		total = total + weight * (ab.x * wall + ab.y);
		weights = weights + weight;
		begin = end;
		u_begin = u_end;
	}
	// Its share of its part, over the band: at the band's own place (`nm_share`: the same for every beam that
	// reads a part of the band, whatever its steps and however little of the band is its), straight from node
	// to node. So the two sides of a cut make the whole in every band, and neither is ever less than nothing.
	float part = dot(b.share, between_weights((map_place(nm_share) - g_lo) / span));
	return weights > 0.0 ? part * total / weights : 0.0;
}

// what the bodies a traced ray met do to the wall's light at nm: a times it plus b (x, y)
vec2 ray_shade(RayMet m, float nm) {
	float room = room_front();
	float a = 1.0;
	float sum = 0.0;
	for (int k = 0; k < 4; k++) {
		if (k >= m.count) {
			break;
		}
		vec4 slot = met_slot(m, k);
		int body = int(slot.x);
		bool bent = slot.y > 0.5;
		Layer l = layer_at(body, met_where(m, k), 1.0e-3, 1.0e-3);
		if (bent) {
			l.haze = haze_along(body, slot.w, met_where(m, k).y);
			if (lies_in(body)) {
				l.solid = slot.y > 2.5 ? 1.0 : 0.0;
			}
		}
		int start = set_of(body) * ROWS_PER_SET * ROW;
		float glow = glow_of(body, start, met_where(m, k), 1.0e-3, 1.0e-3, nm, l.solid);
		vec2 does = body_does(l, row_at(start + ROW, nm), haze_colour(body, start, met_where(m, k).y, nm), glow, row_at(start + 2 * ROW, nm), liquid_at(body, start, nm, met_where(m, k).y) * (bent ? slot.w : l.depth_cm + l.glowing), bent ? slot.z : 1.0, bent ? m.absorb[k] : 0.0, room, heat_of(body, start, met_where(m, k), nm));
		sum = sum + a * does.y;
		a = a * does.x;
	}
	return vec2(a, sum);
}

// where the beam itself puts the ray from the place s of its part (0 to 1 across and down), by the nodes'
// weights w: straight from its middle ray to its edges, as its footprint is read (beam_fan)
vec2 beam_at(Beam b, vec2 s, vec4 w) {
	vec2 c = vec2(dot(b.cx, w), dot(b.cy, w));
	if (b.form == 2) {
		return c;
	}
	float u = s.x - 0.5;
	float v = s.y - 0.5;
	bool upright = abs(v) >= abs(u); // in the triangle above the middle, or below it; else beside it
	float outward = 2.0 * max(abs(u), abs(v)); // 0 at the middle, 1 on the edge
	if (outward < 1.0e-9) {
		return c;
	}
	float along_edge = upright ? (u + abs(v)) / (2.0 * abs(v)) : (v + abs(u)) / (2.0 * abs(u));
	vec2 e0 = upright ? (v < 0.0 ? vec2(dot(b.x0, w), dot(b.y0, w)) : vec2(dot(b.x2, w), dot(b.y2, w))) : (u < 0.0 ? vec2(dot(b.x0, w), dot(b.y0, w)) : vec2(dot(b.x1, w), dot(b.y1, w)));
	vec2 e1 = upright ? (v < 0.0 ? vec2(dot(b.x1, w), dot(b.y1, w)) : vec2(dot(b.x3, w), dot(b.y3, w))) : (u < 0.0 ? vec2(dot(b.x2, w), dot(b.y2, w)) : vec2(dot(b.x3, w), dot(b.y3, w)));
	return vec2(mix(c.x, mix(e0.x, e1.x, along_edge), outward), mix(c.y, mix(e0.y, e1.y, along_edge), outward));
}

// the wall at a place
float wall_point(vec2 at) {
	return wall_span(beam_profile(at.y), at.x, at.x);
}

// One place's correction to the beam, over a band from nm0 to nm1 (its point nm): what a ray traced from the
// place s of the pixel brings, less what the beam itself says of that place. One ray, at the band's point
// (Stage 33; two were traced, at the band's ends). Where it goes the beam's way, the wall is read along a
// line, as the beam's own is: the line the beam's landing sweeps along the band at that place, carried to where
// the ray lands. So a ray that just misses a bright line where the beam's just hits it is a small difference,
// not all of the line's light or none of it (the same mean, and far less to average away). Where it goes
// another way, the wall is read where it lands: the band's point moves through the band with the frames, so
// the mean over them is the band's.
float beam_correction(Beam b, Path q, vec2 s, float nm0, float nm1, float nm, float g_lo, float g_hi) {
	float half_size = 0.5 * q.size;
	int way = ray_way;
	if (b.ways == 1) {
		// the beam's middle ray met no grating: this ray may (a pixel on a grating's edge), and then goes one of
		// its ways, drawn by their shares (the mean over the frames is the sum of them)
		float draw = fract(s.x * 37.0 + s.y * 91.0);
		ray_way = draw < 0.5261408 ? 0 : (draw < 0.7393774 ? 1 : (draw < 0.9526140 ? 2 : (draw < 0.9763070 ? 3 : 4)));
	}
	vec2 p = vec2(q.x - half_size + s.x * q.size, q.y - half_size + s.y * q.size);
	// its branch seed (Stage 78): from its place in the pixel and its wavelength, which move with the frames, so the
	// mean over them is every branch's by its share
	ray_seed = ((uint(fract(s.x) * 65536.0) * 65536u + uint(fract(s.y) * 65536.0)) ^ (uint(nm * 64.0) * 2654435761u)) | 1u;
	RayMet m = ray_met(p, nm);
	ray_seed = 0u;
	ray_way = way;
	vec2 traced = ray_shade(m, nm);
	float span = max(g_hi - g_lo, 1.0e-9);
	bool one_way = beam_nodes_one_way(b);
	vec4 wm = beam_weights(b, (map_place(nm) - g_lo) / span);
	vec2 said = beam_shade(b, wm, nm);
	// (what the beam says of this place is what its band read: the band's sweep where its nodes go one way,
	// the point's own node where they part)
	bool sweeps = !one_way && beam_sweeps(b, (map_place(nm0) - g_lo) / span, (map_place(nm1) - g_lo) / span);
	vec2 at0 = beam_at(b, s, one_way ? thirds_weights((map_place(nm0) - g_lo) / span) : (sweeps ? beam_weights(b, (map_place(nm0) - g_lo) / span) : wm));
	vec2 at1 = beam_at(b, s, one_way ? thirds_weights((map_place(nm1) - g_lo) / span) : (sweeps ? beam_weights(b, (map_place(nm1) - g_lo) / span) : wm));
	float wall_said = wall_span(beam_profile(0.5 * (at0.y + at1.y)), at0.x, at1.x);
	float wall_traced = 0.0;
	if (one_way && m.route == b.c0) {
		vec2 at = beam_at(b, s, wm);
		wall_traced = wall_span(beam_profile(m.y + 0.5 * (at0.y + at1.y) - at.y), m.x + at0.x - at.x, m.x + at1.x - at.x);
	} else {
		wall_traced = wall_point(vec2(m.x, m.y));
	}
	return traced.x * wall_traced + traced.y - (said.x * wall_said + said.y);
}

// The bands a beam is read for, as batch() lays them out: n equal shares of the light's energy from u_first
// to u_last, each cut at the pixel's cuts, its point `offset` of the way through; of each, the part within
// the beam's own stretch of the spectrum (places v_lo to v_hi). Each band's exact colour times what gets
// through (X, Y, Z); or, `single`, each part by its share of the whole (x); or one wavelength alone.
vec3 beam_read(Beam b, float g_lo, float g_hi, float v_lo, float v_hi, int n, float offset, Cuts cuts, float u_first, float u_last, bool single, float one_nm, Path q, int rays) {
	if (one_nm > 0.0) {
		float g = map_place(one_nm);
		bool mine = g >= v_lo && (g < v_hi || v_hi >= 1.0);
		return mine ? vec3(beam_band(b, beam_flat(b), g_lo, g_hi, one_nm, one_nm, 0.0, one_nm, one_nm), 0.0, 0.0) : vec3(0.0);
	}
	float s_lo = v_lo <= 0.0 ? 0.0 : share_of(place_nm(v_lo));
	float s_hi = v_hi >= 1.0 ? 1.0 : share_of(place_nm(v_hi));
	vec3 sum = vec3(0.0);
	float lo = u_first;
	int j = 0;
	// (a beam whose rays part, a rim's or a surface's, has a ray for every band: there the corrections are the
	// picture, and fewer of them is grain that takes longer to fade; measured, 0.75% of pixel-frames visibly
	// wrong against 0.07%)
	int stride = b.form == 2 ? 1 : CORRECTION_STRIDE;
	int turned = int(fract(offset * 89.0 + 0.3) * float(stride)); // which bands have a ray, this frame
	float even = beam_flat(b); // the wall under its footprint, where it is one (`flat` is a word of GLSL's)
	for (int k = 0; k < 4096; k++) {
		if (k >= n) {
			break;
		}
		float band_hi = mix(u_first, u_last, float(k + 1) / float(n));
		for (int r = 0; r <= MAX_CUTS; r++) {
			for (int c = 0; c < MAX_CUTS; c++) {
				if (j >= cuts.count || cuts.u[j] > lo) {
					break;
				}
				j = j + 1;
			}
			float hi = j < cuts.count && cuts.u[j] < band_hi ? cuts.u[j] : band_hi;
			float from = max(lo, s_lo);
			float to = min(hi, s_hi);
			if (to > from) {
				vec4 a = share(from);
				vec4 c = share(to);
				float nm_point = share(mix(from, to, offset)).x + 380.0;
				float light = beam_band(b, even, g_lo, g_hi, a.x + 380.0, c.x + 380.0, offset, nm_point, share(0.5 * (lo + hi)).x + 380.0);
				if (rays > 0 && (k + turned) % stride == 0) {
					// and a traced ray's correction (a pixel as an average over time, below), for one band in
					// CORRECTION_STRIDE, standing for them all (Stage 33: the bands it has turn with the frames, so
					// the mean over them is every band's); from a place of its own for each band, moving with the
					// frame's point, so a batch's places lie evenly over the pixel. (The place moves across by 807
					// times the point and down by 941 times it, and the wavelength within the band by the point itself: three
					// that fill their cube together, the point stepping by 0.618 a frame and the two by 0.753 and 0.570. By 7
					// and 19 a band's rays lay on seven lines of place against wavelength, and a slit's image between two of
					// them was never found: Stage 35. over the frames each band's places cover the pixel. Down
					// by 0.618 of it they lay on one line over 62% of the pixel's height, another for each band: a
					// pixel whose rays go three ways settled on a tint where the truth is grey. Stage 33.)
					float turn = float(k + 1) + 7.0 * float(r);
					corrections_taken = corrections_taken + 1;
					float correction = beam_correction(b, q, vec2(fract(0.7548776662466927 * turn + fract(807.0 * offset)), fract(0.5698402909980532 * turn + fract(941.0 * offset))), a.x + 380.0, c.x + 380.0, nm_point, g_lo, g_hi);
					light = light + float(stride) * correction;
				}
				if (single) {
					sum = sum + vec3((to - from) / max(u_last - u_first, 1.0e-12) * light, 0.0, 0.0);
				} else {
					sum = sum + vec3(c.y - a.y, c.z - a.z, c.w - a.w) * light;
				}
			}
			lo = hi;
			if (hi >= band_hi) {
				break;
			}
		}
	}
	return sum;
}

// Where, between two rays, the way changes: halving from the ray at pa and the place ga of the spectrum
// toward the one at pb and gb. The last share of the way found to go the first one's way (x), and the first
// found not to (y); both 1 where the two go one way.
vec2 beam_cut(vec2 pa, float ga, vec2 pb, float gb) {
	float lo = 0.0;
	float hi = 1.0;
	uint route = 0u;
	for (int it = 0; it < 12; it++) {
		// the two ends first, then the halvings
		float mid = it == 0 ? 0.0 : (it == 1 ? 1.0 : 0.5 * (lo + hi));
		uint here = ray_met(vec2(mix(pa.x, pb.x, mid), mix(pa.y, pb.y, mid)), place_nm(mix(ga, gb, mid))).route;
		if (it == 0) {
			route = here;
		} else if (it == 1) {
			if (here == route) {
				return vec2(1.0, 1.0);
			}
		} else if (here == route) {
			lo = mid;
		} else {
			hi = mid;
		}
	}
	return vec2(lo, hi);
}

// A pixel's light through what bends its view, by its beams: over the bands batch() reads (n of them, their
// points at `offset`, cut at `cuts`: X, Y, Z), or over one band from u_first to u_last (`single`: its light
// in x), or at one wavelength (x). In w: how much of the pixel the beams read made up (1: all of it).
vec4 beam_sum(Path q, int n, float offset, Cuts cuts, float u_first, float u_last, bool single, float one_nm) {
	vec3 sum = vec3(0.0);
	float made = 0.0;
	float half_size = 0.5 * q.size;
	float x_left = q.x - half_size;
	float y_top = q.y - half_size;
	// The stretch of the spectrum in hand, from the place g_lo to g_hi (its nodes'): all of it, if four nodes
	// speak for it. Where the middle ray's way changes along it, it is cut there; where the ray traced half way
	// between its nodes is not where their cubic puts it (a prism spreads the light too far for four), it is
	// halved; and the next stretch, from g_next on, is tried twice as long as the last. The places a stretch is
	// read over (piece_lo to piece_hi) are its nodes' own, but up to the middle of a cut, so stretches meet.
	float g_lo = 0.0;
	float g_hi = 1.0;
	float g_next = 1.0;
	float piece_lo = 0.0;
	float piece_hi = 1.0;
	float cut_to = 0.0;
	if (one_nm > 0.0) {
		// one wavelength alone: its rays are traced at that wavelength itself, and nothing is read between nodes
		g_lo = map_place(one_nm);
		g_hi = g_lo;
		g_next = g_lo;
	}
	// where the reading is: 0 the pixel, 1 a quarter (iq), 2 a quarter of that (is), 3 a side of a cut
	// sixteenth (ie: its edge, then its side), 4 one ray of a side (in: the node)
	int level = 0;
	int iq = 0;
	int is = 0;
	int ie = 0;
	int in_ = 0;
	bool across = true; // the sixteenth's rays part across (its top and bottom edges are cut), or up and down (its sides)
	// where each of its two cut edges' rays part, at each node: the last share of the way along the edge that
	// goes its start's way, and the first that does not
	vec4 cut0_lo = vec4(1.0);
	vec4 cut0_hi = vec4(1.0);
	vec4 cut1_lo = vec4(1.0);
	vec4 cut1_hi = vec4(1.0);
	// cuts still to find before reading on (found at one place, as the beams are traced at one): 9, where the
	// middle ray's way changes along the spectrum, from the place cut_from on; 8 to 1, where the rays of a
	// sixteenth's two edges part, at each node
	int pending = 0;
	float cut_from = 0.0;
	int leaf = BEAM_DEEPEST;
	bool second_none = false;
	// what had been read when the part in hand was cut: if a side of it turns out bent (its middle ray off the
	// mean of its corners'), the part is not read by its sides after all, but by its quarters
	vec3 sum_at_cut = vec3(0.0);
	float made_at_cut = 0.0;
	int spent = 0; // rays followed so far
	bool lies = false; // the part in hand is read as it lies: by its middle ray alone
	// (one loop, read straight down: nothing in it jumps back but the loop itself; the compiler that builds the
	// pass for the GPU did not finish with the same reading written with jumps)
	bool finished = false;
	for (int guard = 0; guard < 16384; guard++) {
		if (finished) {
			break;
		}
		// the part in hand
		// (a side, and a ray of it, are of the part that was cut: `leaf`, the level it is at)
		int cell = level >= 3 ? leaf : level;
		float size = cell == 0 ? q.size : (cell == 1 ? half_size : 0.5 * half_size);
		float x0 = x_left + (cell >= 1 ? float(iq % 2) * half_size : 0.0) + (cell >= 2 ? float(is % 2) * 0.5 * half_size : 0.0);
		float y0 = y_top + (cell >= 1 ? float(iq / 2) * half_size : 0.0) + (cell >= 2 ? float(is / 2) * 0.5 * half_size : 0.0);
		float weight = cell == 0 ? 1.0 : (cell == 1 ? 0.25 : 0.0625);
		vec2 p0 = vec2(x0, y0);
		vec2 p1 = vec2(x0 + size, y0);
		vec2 p2 = vec2(x0, y0 + size);
		vec2 p3 = vec2(x0 + size, y0 + size);
		vec2 pc = vec2(x0 + 0.5 * size, y0 + 0.5 * size);
		int kind = 0;
		float v_lo = piece_lo;
		float v_hi = piece_hi;
		vec4 fa = vec4(0.0);
		vec4 fb = vec4(1.0);
		vec4 part = vec4(1.0); // a side's share of its edge at each node: the two sides of a cut make the whole edge
		int side = 0;
		if (level >= 3) {
			// a side of the cut part: up to where its two cut edges' rays part, or from there on; its share of
			// the part, at each node, the mean of its shares of the two edges
			bool second = ie == 1;
			side = lies ? 4 : (across ? 0 : 2) + (second ? 1 : 0);
			if (!lies) {
				fa = second ? cut0_hi : cut0_lo;
				fb = second ? cut1_hi : cut1_lo;
				part = vec4(0.25 * (cut0_lo.x + cut0_hi.x + cut1_lo.x + cut1_hi.x), 0.25 * (cut0_lo.y + cut0_hi.y + cut1_lo.y + cut1_hi.y), 0.25 * (cut0_lo.z + cut0_hi.z + cut1_lo.z + cut1_hi.z),
						0.25 * (cut0_lo.w + cut0_hi.w + cut1_lo.w + cut1_hi.w));
				if (second) {
					part = vec4(1.0 - part.x, 1.0 - part.y, 1.0 - part.z, 1.0 - part.w);
				}
				weight = weight * (max(max(part.x, part.y), max(part.z, part.w)) > 0.0 ? 1.0 : 0.0);
			}
			kind = 1;
			if (level == 4) {
				// one ray, at one node: its own stretch of the spectrum, half way to the nodes beside it
				kind = 2;
				v_lo = in_ == 0 ? piece_lo : mix(g_lo, g_hi, float(in_) / 3.0 - 1.0 / 6.0);
				v_hi = in_ == 3 ? piece_hi : mix(g_lo, g_hi, float(in_) / 3.0 + 1.0 / 6.0);
				if (!(g_hi > g_lo)) {
					// one wavelength alone: the four nodes are one, and the first speaks for all
					v_lo = piece_lo;
					v_hi = in_ == 0 ? piece_hi : piece_lo;
				}
			}
		}
		// what comes of this turn: 0, the part is read (or is nothing): on to the next; 1, into its parts; 2, the
		// same again (a cut was found, or is to be)
		int turn = 0;
		if (pending > 0) {
			turn = 2;
			if (pending == 9) {
				vec2 change = beam_cut(pc, cut_from, pc, cut_to);
				g_next = mix(cut_from, cut_to, change.y);
				g_hi = mix(cut_from, cut_to, change.x);
				piece_hi = 0.5 * (g_hi + g_next);
				pending = 0;
			} else {
				// (an edge's cut at the spectrum's two ends first, then between them: found there too only where the
				// ends' differ, which is where the cut moves with the wavelength; an outline's does not)
				int e = (8 - pending) / 4;
				int turn_w = (8 - pending) % 4;
				int w = turn_w == 0 ? 0 : (turn_w == 1 ? 3 : turn_w - 1);
				vec2 ea = e == 0 ? p0 : (across ? p2 : p1);
				vec2 eb = e == 0 ? (across ? p1 : p2) : p3;
				float g = mix(g_lo, g_hi, float(w) / 3.0);
				vec4 found_lo = e == 0 ? cut0_lo : cut1_lo;
				vec4 found_hi = e == 0 ? cut0_hi : cut1_hi;
				bool still = turn_w >= 2 && abs(found_lo.x - found_lo.w) < 0.003 && abs(found_hi.x - found_hi.w) < 0.003;
				vec2 cut = vec2(mix(found_lo.x, found_lo.w, float(w) / 3.0), mix(found_hi.x, found_hi.w, float(w) / 3.0));
				if (e == 1 && second_none) {
					cut = vec2(0.0, 0.0);
				} else if (!still) {
					cut = beam_cut(ea, g, eb, g);
					spent = spent + 12;
				}
				if (e == 0) {
					cut0_lo[w] = cut.x;
					cut0_hi[w] = cut.y;
				} else {
					cut1_lo[w] = cut.x;
					cut1_hi[w] = cut.y;
				}
				pending = pending - 1;
				if (pending == 0) {
					level = 3;
					ie = 0;
				}
			}
		} else if (weight > 0.0) {
			Beam b = beam_trace(kind, side, p0, p1, p2, p3, pc, fa, fb, g_lo, g_hi, in_, level == 0);
			b.share = part;
			spent = spent + (kind == 2 ? 1 : 20);
			// (a stretch is not made shorter than a 16th of the spectrum: then it is read as it is. A ray that a
			// rim's bead throws about lands pixels away for a nanometre's change: no stretch is short enough)
			bool long_enough = level == 0 && g_hi - g_lo > 1.0 / 16.0;
			bool ways = !(b.c0 == b.c1 && b.c1 == b.c2 && b.c2 == b.c3);
			vec4 half_weights = thirds_weights(1.0 / 6.0);
			float sweep = max(abs(b.cx.w - b.cx.x), abs(b.cy.w - b.cy.x));
			bool missed = BEAM_BETWEEN > 0.0 && (b.pr != b.c0 || max(abs(dot(b.cx, half_weights) - b.px), abs(dot(b.cy, half_weights) - b.py)) > BEAM_BETWEEN * max(b.reach, 1.0) + 0.01 * sweep);
#ifdef OPTICS_STRETCH_HOOK
			if (level == 0 && !(long_enough && (ways || missed))) {
				OPTICS_STRETCH_HOOK(g_lo, g_hi);
			}
#endif
			if (long_enough && ways) {
				// the middle ray's way changes along the stretch: cut there, and read each side of it
				float third = b.c0 != b.c1 ? 0.0 : (b.c1 != b.c2 ? 1.0 / 3.0 : 2.0 / 3.0);
				cut_from = mix(g_lo, g_hi, third);
				cut_to = mix(g_lo, g_hi, third + 1.0 / 3.0);
				pending = 9;
				turn = 2;
			} else if (long_enough && missed) {
				// four nodes do not speak for the stretch: its first half
				g_hi = mix(g_lo, g_hi, 0.5);
				g_next = g_hi;
				piece_hi = g_hi;
				turn = 2;
			} else if (level < 3 && !b.one && spent > BEAM_RAYS_MOST) {
				// enough rays followed: the part as it lies, by its middle ray at each node
				leaf = level;
				lies = true;
				ie = 1;
				level = 4;
				in_ = 0;
				turn = 2;
			} else if (level == 3 && leaf < BEAM_DEEPEST && !(b.one && b.straight) && spent <= BEAM_RAYS_MOST) {
				// a side of a part that could be made smaller, and is bent or not all one way: the part's quarters
				sum = sum_at_cut;
				made = made_at_cut;
				level = leaf + 1;
				if (level == 1) {
					iq = 0;
				} else {
					is = 0;
				}
				turn = 2;
			} else if (level == 4 || (level >= BEAM_DEEPEST && b.one) || (b.one && b.straight)) {
				// a part is read whole where its rays go one way and its middle ray lands at the mean of its
				// corners'; a sixteenth and a side, where their rays go one way; one ray, always
				sum = sum + beam_read(b, g_lo, g_hi, v_lo, v_hi, n, offset, cuts, u_first, u_last, single, one_nm, q, 0) * weight;
				made = made + weight * (v_hi - v_lo) * 0.25 * (b.share.x + b.share.y + b.share.z + b.share.w);
			} else if (level < 3 && (level == BEAM_DEEPEST || (level >= BEAM_CUT_AT && ((b.r0 == b.r2 && b.r1 == b.r3 && b.r0 != b.r1) || (b.r0 == b.r1 && b.r2 == b.r3 && b.r0 != b.r2))))) {
				// A part whose rays part is cut where they do, into two sides: across (its left corners go one
				// way and its right ones another: its top and its bottom edges are cut), or up and down (its
				// left and its right edges). As small as parts are made, it is cut whichever way its corners
				// lie: a side that cannot be all one way is then read ray by ray.
				leaf = level;
				across = b.r0 != b.r1 || b.r2 != b.r3 || !(b.r0 != b.r2 || b.r1 != b.r3);
				// (where its second edge does not begin the first one's way, the first's way has none of it)
				second_none = across ? b.r2 != b.r0 : b.r1 != b.r0;
				sum_at_cut = sum;
				made_at_cut = made;
				lies = false;
				pending = 8;
				turn = 2;
			} else {
				turn = 1;
			}
		}
		if (turn == 1) {
			level = level + 1;
			if (level == 1) {
				iq = 0;
			} else if (level == 2) {
				is = 0;
			} else if (level == 3) {
				ie = 0;
			} else {
				in_ = 0;
			}
		} else if (turn == 0) {
			// the next part: the next of its kind, or, after the last of them, the next of the kind above
			bool carry = true;
			if (level == 4) {
				in_ = in_ + 1;
				carry = in_ >= 4;
				if (carry) {
					in_ = 0;
					level = 3;
				}
			}
			if (carry && level == 3) {
				ie = ie + 1;
				carry = ie >= 2;
				if (carry) {
					level = leaf;
				}
			}
			if (carry && level == 2) {
				is = is + 1;
				carry = is >= 4;
				if (carry) {
					level = 1;
				}
			}
			if (carry && level == 1) {
				iq = iq + 1;
				carry = iq >= 4;
				if (carry) {
					level = 0;
				}
			}
			if (carry && level == 0) {
				if (piece_hi < 1.0) {
					// the next stretch of the spectrum: tried twice as long as the last, to the spectrum's end if
					// that is near
					float stretch = piece_hi - piece_lo;
					piece_lo = piece_hi;
					g_lo = g_next;
					g_hi = g_lo + 2.0 * stretch;
					if (g_hi > 1.0 - 0.25 * stretch) {
						g_hi = 1.0;
					}
					g_next = g_hi;
					piece_hi = g_hi;
				} else {
					finished = true;
				}
			}
		}
	}
	return vec4(sum.x, sum.y, sum.z, made);
}

// ---- a pixel as an average over time (GAME_SPEC §12.3, Stage 29)
//
// A pixel's light is one integral: over its area and the spectrum, of what a ray's way lets through times the
// wall where the ray lands. Any average of rays traced at places and wavelengths drawn evenly from them comes
// to it (it is the truth's own sum, taken a few rays a frame). Alone that average is noisy wherever the wall
// has an edge. But the pixel's beam is a smooth stand-in for the same integrand whose integral is read exactly
// (beam_band: the wall over the footprint, every edge counted). So the pixel is
//     the beam's exact reading  +  the average of (a traced ray's light - the beam's own light at that ray's place)
// whose mean is the integral whatever the beam is like: where the glass is smooth the difference is nothing
// and the pixel is right at once; where rays part (a rim, an outline, a liquid's surface, a prism's spread) the
// difference is the correction, and its average converges as the frames go by. Every pixel costs the same: a
// beam (twenty rays) when what it looks through changes, and a ray for one band in four.

// A pixel's light as its beam's exact reading and, with each band, one traced ray's correction (`rays` 1; 0: the
// beam's reading alone, the fixed estimate): over the bands batch() reads (X, Y, Z), or one band from u_first
// to u_last (`single`: x). The beam is traced over a stretch of the spectrum that its four nodes speak for:
// where the ray traced between them is not where their cubic puts it, or its way changes along the stretch,
// the stretch is halved (to a sixteenth of the spectrum at least), and the next is tried twice as long. A
// vessel's pixel is one stretch; a prism's, which spreads the light far, several. The stand-in need not be
// good for the mean to be right; it is made good so that the corrections are small.
// (Both at once where a pixel's content has just changed: the fixed estimate, eight bands at their middles with
// no ray more, and the frame's batch, from one tracing of the beam.)
struct BeamPair {
	vec3 fixed_estimate;
	vec3 moving;
};

// ---- beams kept (GAME_SPEC §12.3, Stage 33)
//
// A pixel's beams are what its rays do, and its rays go the same way until what it looks through moves: its
// shape key (path_of: the shape tags of the bodies over it and of those its rays can reach behind them). So a
// beam is traced when that key moves, not whenever the pixel reads: it is written to the pixel's record (the
// includer's kept_vec and kept_put; `kept_slot`, where the pixel's record
// begins, or -1 for a pixel with none), and read from there while the key is the same. A change of colour
// alone (a reaction, a haze) traces nothing; a pixel not yet settled traces only its corrections. The record:
// [0] the shape key it was traced for (x, y), the pixel (z), how many beams it holds (w; 0: none yet; -1:
// more than fit, and then they are traced at every read as before). Then each beam, KEPT_BEAM vec4s: its stretch of the
// spectrum and its share (a grating's way); its corners' and its middle's landings at the nodes; what its
// middle ray met; what their surfaces passed and its chords; and the rest.
//
// And a pixel whose beam has just been traced, with what it looks through changed, is its beam's reading alone
// for that frame: its corrections' rays wait until its rays have been the same for a frame. While a thing
// moves its pixels cost a beam each; once it is still they take their corrections as ever, and settle to the
// same picture. A pixel whose colour alone changes has its beam kept and takes its corrections at once: what
// changes colour for minutes (a reaction) must not go without them.
OPTICS_THREAD int kept_slot = -1;
OPTICS_THREAD bool beam_deferred = false; // the pixel's beam was traced now, and its first corrections wait
OPTICS_THREAD bool beam_held = false; // the pixel's beam was kept, and its fixed estimate alone was read
OPTICS_THREAD bool beam_one_way = false; // and every beam of it is of rays that go one way
OPTICS_THREAD bool kept_read = true; // (false: a record is written and never read, for holding the two to each other)
const int KEPT_BEAM = 25;
#ifndef KEPT_MOST
#define KEPT_MOST 1
#endif

void keep_beam(int at, Beam b, float g_lo, float g_hi, float share, float content) {
	kept_put(at, vec4(g_lo, g_hi, share, float(b.form) + (b.one ? 4.0 : 0.0) + (b.straight ? 8.0 : 0.0)));
	kept_put(at + 1, b.x0);
	kept_put(at + 2, b.y0);
	kept_put(at + 3, b.x1);
	kept_put(at + 4, b.y1);
	kept_put(at + 5, b.x2);
	kept_put(at + 6, b.y2);
	kept_put(at + 7, b.x3);
	kept_put(at + 8, b.y3);
	kept_put(at + 9, b.cx);
	kept_put(at + 10, b.cy);
	kept_put(at + 11, b.m0);
	kept_put(at + 12, b.m1);
	kept_put(at + 13, b.m2);
	kept_put(at + 14, b.m3);
	kept_put(at + 15, b.bent);
	kept_put(at + 16, b.pass0);
	kept_put(at + 17, b.chord0);
	kept_put(at + 18, b.pass1);
	kept_put(at + 19, b.chord1);
	kept_put(at + 20, vec4(float(b.layers), float(b.ways), b.bend, b.reach));
	// (a route is 32 bits: in two halves, each a whole number a float holds)
	kept_put(at + 21, vec4(float(b.c0 & 65535u), float(b.c0 >> 16), (b.c0 == b.c1 ? 1.0 : 0.0) + (b.c1 == b.c2 ? 2.0 : 0.0) + (b.c2 == b.c3 ? 4.0 : 0.0), 0.0));
	// the rows its rays passed in bodies that something lies in (all, where its rays part: what the corrections
	// found for it came by other ways), and what lay in them
	kept_put(at + 22, vec4(b.one ? b.span.x : -1.0e30, b.one ? b.span.y : 1.0e30, content, 0.0));
	kept_put(at + 23, b.absorb0);
	kept_put(at + 24, b.absorb1);
}

Beam kept_beam(int at) {
	Beam b;
	vec4 head = kept_vec(at);
	vec4 rest = kept_vec(at + 20);
	vec4 route = kept_vec(at + 21);
	b.x0 = kept_vec(at + 1);
	b.y0 = kept_vec(at + 2);
	b.x1 = kept_vec(at + 3);
	b.y1 = kept_vec(at + 4);
	b.x2 = kept_vec(at + 5);
	b.y2 = kept_vec(at + 6);
	b.x3 = kept_vec(at + 7);
	b.y3 = kept_vec(at + 8);
	b.cx = kept_vec(at + 9);
	b.cy = kept_vec(at + 10);
	b.m0 = kept_vec(at + 11);
	b.m1 = kept_vec(at + 12);
	b.m2 = kept_vec(at + 13);
	b.m3 = kept_vec(at + 14);
	b.layers = int(rest.x);
	b.l0 = b.layers > 0 ? beam_layer(b.m0) : layer_none();
	b.l1 = b.layers > 1 ? beam_layer(b.m1) : layer_none();
	b.l2 = b.layers > 2 ? beam_layer(b.m2) : layer_none();
	b.l3 = b.layers > 3 ? beam_layer(b.m3) : layer_none();
	b.bent = kept_vec(at + 15);
	b.pass0 = kept_vec(at + 16);
	b.chord0 = kept_vec(at + 17);
	b.pass1 = kept_vec(at + 18);
	b.chord1 = kept_vec(at + 19);
	b.absorb0 = kept_vec(at + 23);
	b.absorb1 = kept_vec(at + 24);
	b.c0 = uint(route.x) + (uint(route.y) << 16);
	// (its other nodes' routes are kept as whether they are the first's: route.z)
	// (its other nodes' routes are kept as which neighbours go one way: route.z, a bit a piece)
	float same = route.z;
	bool last = same > 3.5;
	same = same - (last ? 4.0 : 0.0);
	bool middle = same > 1.5;
	same = same - (middle ? 2.0 : 0.0);
	b.c1 = same > 0.5 ? b.c0 : b.c0 ^ 1u;
	b.c2 = middle ? b.c1 : b.c1 ^ 2u;
	b.c3 = last ? b.c2 : b.c2 ^ 4u;
	b.r0 = b.c0;
	b.r1 = b.c0;
	b.r2 = b.c0;
	b.r3 = b.c0;
	b.straight = head.w > 7.5;
	b.one = head.w - (b.straight ? 8.0 : 0.0) > 3.5;
	b.form = int(head.w - (b.straight ? 8.0 : 0.0) - (b.one ? 4.0 : 0.0));
	b.bend = rest.z;
	b.off = 0.0;
	b.reach = rest.w;
	b.px = 0.0;
	b.py = 0.0;
	b.pr = b.c0;
	b.ways = int(rest.y);
	b.share = vec4(1.0);
	vec4 passed = kept_vec(at + 22);
	b.span = vec2(passed.x, passed.y);
	return b;
}

BeamPair beam_means(Path q, int n, float offset, Cuts cuts, float u_first, float u_last, bool single, int rays, bool fixed_too) {
	BeamPair pair;
	pair.fixed_estimate = vec3(0.0);
	float half_size = 0.5 * q.size;
	vec2 p0 = vec2(q.x - half_size, q.y - half_size);
	vec2 p1 = vec2(q.x + half_size, q.y - half_size);
	vec2 p2 = vec2(q.x - half_size, q.y + half_size);
	vec2 p3 = vec2(q.x + half_size, q.y + half_size);
	vec3 sum = vec3(0.0);
	float g_lo = 0.0;
	float g_hi = 1.0;
	// the way its rays go on from a grating, if they meet one: each way in turn, by its share (the first beam
	// says how many there are)
	int way = 0;
	int ways = 1;
	bool finished = false;
	// its beams as they were kept, while what it looks through is where it was; else traced, and kept
	bool keeping = kept_slot >= 0 && !single && u_first <= 0.0 && u_last >= 1.0;
	float pixel = floor(q.x) + 1920.0 * floor(q.y);
	int kept = 0;
	bool fits = true;
	if (keeping) {
		vec4 head = kept_vec(kept_slot);
		bool held = head.x == q.shape.x && head.y == q.shape.y && head.z == pixel && head.w > 0.5;
		if (held && kept_vec(kept_slot + 1 + 22).z != q.content.x) {
			// What lies in the bodies it looks through has changed (Stage 33). Its rays go as they went if it
			// changed only on rows they do not pass (a level that rises under them, or over them): then the
			// record stands, for what lies there now. A row and a quarter of its rays' own spread are left
			// between: what its corrections found came by rays beside these.
			float lo = 1.0e30;
			float hi = -1.0e30;
			for (int i = 0; i < KEPT_MOST; i++) {
				if (i >= int(head.w)) {
					break;
				}
				vec4 passed = kept_vec(kept_slot + 1 + i * KEPT_BEAM + 22);
				lo = min(lo, passed.x);
				hi = max(hi, passed.y);
			}
			float beside = 1.0 + 0.25 * max(hi - lo, 0.0);
			held = kept_vec(kept_slot + 1 + 22).z == q.content.y && (hi < lo || hi + beside < q.zone.x || lo - beside > q.zone.y);
			if (held) {
				for (int i = 0; i < KEPT_MOST; i++) {
					if (i >= int(head.w)) {
						break;
					}
					vec4 passed = kept_vec(kept_slot + 1 + i * KEPT_BEAM + 22);
					kept_put(kept_slot + 1 + i * KEPT_BEAM + 22, vec4(passed.x, passed.y, q.content.x, 0.0));
				}
			}
		}
		if (held && fixed_too) {
			// its rays go as they went, and only its colour can have changed: its fixed estimate is read again,
			// and its points are by need, as any pixel's (the history says whether it still holds)
			beam_held = true;
			beam_one_way = true;
			for (int i = 0; i < KEPT_MOST; i++) {
				if (i >= int(head.w)) {
					break;
				}
				float kind = kept_vec(kept_slot + 1 + i * KEPT_BEAM).w;
				if (kind - (kind > 7.5 ? 8.0 : 0.0) < 3.5) {
					beam_one_way = false; // (its rays part)
				}
			}
			n = 0;
		}
		if (held && kept_read) {
			for (int i = 0; i < KEPT_MOST; i++) {
				if (i >= int(head.w)) {
					break;
				}
				int at = kept_slot + 1 + i * KEPT_BEAM;
				vec4 stretch = kept_vec(at);
				Beam b = kept_beam(at);
				if (n > 0) {
					sum = sum + beam_read(b, stretch.x, stretch.y, stretch.x, stretch.y, n, offset, cuts, u_first, u_last, single, 0.0, q, rays) * stretch.z;
				}
				if (fixed_too) {
					fan_quad = true;
					pair.fixed_estimate = pair.fixed_estimate + beam_read(b, stretch.x, stretch.y, stretch.x, stretch.y, PROXY_POINTS, 0.5, cuts, 0.0, 1.0, false, 0.0, q, 0) * stretch.z;
					fan_quad = false;
				}
			}
			pair.moving = sum;
			return pair;
		}
		// traced now, its rays having moved: its first corrections wait a frame (unless its beams were found
		// not to fit a record: such a pixel is traced at every read, and cannot tell its rays' moving from its
		// colour's changing)
		if (fixed_too && !held && !(head.z == pixel && head.w < -0.5)) {
			beam_deferred = true;
			n = 0;
		}
	}
	for (int trial = 0; trial < 256; trial++) {
		if (finished) {
			break;
		}
		ray_way = way;
		Beam b = beam_trace(0, 0, p0, p1, p2, p3, vec2(q.x, q.y), vec4(0.0), vec4(1.0), g_lo, g_hi, 0, true);
		if (way == 0 && g_lo <= 0.0) {
			ways = b.ways;
		}
		// (a stretch is halved only for a beam whose rays go one way: there a better stand-in makes the
		// corrections small. Where they part, no stretch is short enough, and halving would cost a pixel on a
		// rim hundreds of rays for nothing: the corrections bring what the beam lacks.)
		vec4 between_weights = thirds_weights(1.0 / 6.0);
		float sweep = max(abs(b.cx.w - b.cx.x), abs(b.cy.w - b.cy.x));
		// (the stand-in is held to the sweep's tolerance on the wall, and to a 500th of how far its landing sweeps
		// along the stretch: a slit's image misplaced by a pixel is a correction as bright as the slit, and a
		// picture that takes seconds to lose its grain)
		bool missed = BEAM_BETWEEN > 0.0 && (b.pr != b.c0 || max(abs(dot(b.cx, between_weights) - b.px), abs(dot(b.cy, between_weights) - b.py)) > BEAM_BETWEEN * max(b.reach, 1.0) + 0.002 * sweep);
		if (g_hi - g_lo > 1.0 / 64.0 && b.one && missed) {
			g_hi = mix(g_lo, g_hi, 0.5);
		} else {
#ifdef OPTICS_STRETCH_HOOK
			if (b.one) {
				OPTICS_STRETCH_HOOK(g_lo, g_hi);
			}
#endif
			if (!b.one) {
				// its rays part: the beam is its middle ray alone (the corrections bring the rest)
				b.x0 = b.cx;
				b.y0 = b.cy;
				b.x1 = b.cx;
				b.y1 = b.cy;
				b.x2 = b.cx;
				b.y2 = b.cy;
				b.x3 = b.cx;
				b.y3 = b.cy;
				b.form = 2;
				b.bend = 0.0;
			}
			float share_of_way = ways > 1 ? way_share(way) : 1.0;
			if (keeping) {
				if (kept < KEPT_MOST) {
					keep_beam(kept_slot + 1 + kept * KEPT_BEAM, b, g_lo, g_hi, share_of_way, q.content.x);
					kept = kept + 1;
				} else {
					fits = false;
				}
			}
			if (n > 0) {
				sum = sum + beam_read(b, g_lo, g_hi, g_lo, g_hi, n, offset, cuts, u_first, u_last, single, 0.0, q, rays) * share_of_way;
			}
			if (fixed_too) {
				fan_quad = true;
				pair.fixed_estimate = pair.fixed_estimate + beam_read(b, g_lo, g_hi, g_lo, g_hi, PROXY_POINTS, 0.5, cuts, 0.0, 1.0, false, 0.0, q, 0) * share_of_way;
				fan_quad = false;
			}
			if (g_hi >= 1.0) {
				// the spectrum is read in this way: the next way, or all is read
				way = way + 1;
				g_lo = 0.0;
				g_hi = 1.0;
				finished = way >= ways;
			} else {
				float stretch = g_hi - g_lo;
				g_lo = g_hi;
				g_hi = g_lo + 2.0 * stretch;
				if (g_hi > 1.0 - 0.25 * stretch) {
					g_hi = 1.0;
				}
			}
		}
	}
	ray_way = 0;
	if (keeping) {
		kept_put(kept_slot, vec4(q.shape.x, q.shape.y, pixel, fits ? float(kept) : -1.0));
	}
	pair.moving = sum;
	return pair;
}

vec4 beam_mean(Path q, int n, float offset, Cuts cuts, float u_first, float u_last, bool single, int rays) {
	vec3 light = beam_means(q, n, offset, cuts, u_first, u_last, single, rays, false).moving;
	return vec4(light.x, light.y, light.z, 1.0);
}

// whether the pixel is read by its own rays (Stage 29): through anything that bends its view
bool beamed(Path q) {
	return q.refractor >= 0 || q.ways;
}

Cuts cuts_none();

// What a pass costs on the GPU follows how much there is in its shader a pixel could do, done or not. So the
// host compiles this file once for each of its passes: with OPTICS_SCREEN_PASS for the pass over the whole
// screen, which leaves the pixels read by their own rays alone and has nothing of their reading; with
// OPTICS_BEAM_PASS for the pass over the rectangles of what bends (lab/optics_scene.hpp), which reads those
// pixels and no others. The reference defines neither, and reads every pixel.

// The share of the room's light that reaches the eye over a band of its energy (its point t of the way through),
// or at one wavelength, as a picture reads it.
float band_light(Path q, float u0, float u1, float t, float one_nm) {
#ifndef OPTICS_SCREEN_PASS
	if (beamed(q)) {
		if (!(one_nm > 0.0)) {
			return beam_mean(q, 1, t, cuts_none(), u0, u1, true, 1).x;
		}
		// one wavelength: read exactly, each part followed to where its rays part (the limit the average over
		// time comes to; the reference's checks are of it)
		if (q.ways) {
			// through a grating: each of its ways in turn, by its share
			float light = 0.0;
			for (int way = 0; way < GRATING_WAYS; way++) {
				ray_way = way;
				light = light + way_share(way) * beam_sum(q, 1, t, cuts_none(), u0, u1, true, one_nm).x;
			}
			ray_way = 0;
			return light;
		}
		return beam_sum(q, 1, t, cuts_none(), u0, u1, true, one_nm).x;
	}
#endif
	return flat_band(q, u0, u1, t, one_nm);
}

// ---- the light in bands of equal energy

vec4 share(float u) {
	float x = clamp(u, 0.0, 1.0) * 4096.0;
	int j = min(int(floor(x)), 4095) + band_range * LIGHT_SIZE; // (the range's light: Stage 46)
	return mix(light_vec(j), light_vec(j + 1), x - float(min(int(floor(x)), 4095)));
}

vec3 to_linear_srgb(vec3 c) {
	return vec3(3.2404542 * c.x - 1.5371385 * c.y - 0.4985314 * c.z, -0.9692660 * c.x + 1.8760108 * c.y + 0.0415560 * c.z, 0.0556434 * c.x - 0.2040259 * c.y + 1.0572252 * c.z);
}

// Where a pixel's light spikes in wavelength within a band (GAME_SPEC §12.2): the shares of the light's
// energy at which it does, ascending. A band cut there is read on each side of the spike, by the moving
// points and by the fixed estimate alike. Only a narrow absorption line in what the pixel looks through
// needs it (its core, a width and a half to each side): a bent view sweeping across the wall's edges is
// integrated exactly (core_light), and a light's own narrow lines have their shares in its energy table.

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
	vec4 ids = scene_vec(HEADER_VECS + body_of(l.body) * BODY_VECS + 4);
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

Cuts cuts_none() {
	Cuts c;
	c.count = 0;
	for (int i = 0; i < MAX_CUTS; i++) {
		c.u[i] = 1.0;
	}
	return c;
}

Cuts cuts_of(Path q) {
	Cuts c;
	c.count = 0;
	for (int i = 0; i < MAX_CUTS; i++) {
		c.u[i] = 1.0;
	}
	if (scene_vec(2).w < 0.5 || band_range == 1) {
		return c; // (narrow lines are the visible's)
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
#ifdef OPTICS_BEAM_PASS
	// the pass over the pixels read by their own rays reads nothing else
	// (the fixed estimate is the eight bands read at their middles: the beam's own reading, no ray more)
	vec4 beams = beam_mean(q, n, offset, cuts, 0.0, 1.0, false, n == PROXY_POINTS && offset == 0.5 ? 0 : 1);
	return to_linear_srgb(vec3(beams.x, beams.y, beams.z));
#else
#ifndef OPTICS_SCREEN_PASS
	if (beamed(q)) {
		// (the fixed estimate is the eight bands read at their middles: the beam's own reading, no ray more)
		vec4 light = beam_mean(q, n, offset, cuts, 0.0, 1.0, false, n == PROXY_POINTS && offset == 0.5 ? 0 : 1);
		return to_linear_srgb(vec3(light.x, light.y, light.z));
	}
#endif
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
			sum = sum + vec3(upto.y - before.y, upto.z - before.z, upto.w - before.w) * flat_band(q, lo, hi, offset, 0.0);
			before = upto;
			lo = hi;
			if (hi >= band_hi) {
				break;
			}
		}
	}
	return to_linear_srgb(sum);
#endif
}

// the colour itself: the light's shares, each its own band, read at its middle
vec3 truth(Path q, int shares) {
#ifndef OPTICS_SCREEN_PASS
	if (beamed(q)) {
		vec4 light = beam_mean(q, shares, 0.5, cuts_none(), 0.0, 1.0, false, 1);
		return to_linear_srgb(vec3(light.x, light.y, light.z));
	}
#endif
	vec3 sum = vec3(0.0);
	for (int j = 0; j < 4096; j++) {
		if (j >= shares) {
			break;
		}
		vec4 a = share(float(j) / float(shares));
		vec4 b = share(float(j + 1) / float(shares));
		float lo = float(j) / float(shares);
		float hi = float(j + 1) / float(shares);
		sum = sum + vec3(b.y - a.y, b.z - a.z, b.w - a.w) * flat_band(q, lo, hi, 0.5, 0.0);
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

// what a beam traced in a frame is counted as, of the rays' budget (points): a carried vessel's pixels take all
// of it, and what is settling beside it waits until it rests (Stage 33)
const float RAY_POINTS_A_BEAM = 8.0;

#define BATCH_TILE_X 2
#define BATCH_TILE_Y 32

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
	// A pixel read by its own rays (Stage 29) traces its beam once for both: the fixed estimate, and with it the
	// frame's batch, which is then the least batch whatever is asked (its rays are the cost, and a pixel whose
	// content keeps changing has them every frame).
	bool both = false;
	vec3 early = vec3(0.0);
	corrections_taken = 0;
	float seed = fract(0.7548776662 * p.x + 0.56984029 * p.y);
	float moving_point = fract(seed + 0.6180339887 * float(frame % 8192));
	bool kept_whole = false;
	beam_deferred = false;
	beam_held = false;
	beam_one_way = false;
	if (s.key.x != q.key.x || s.key.y != q.key.y || s.key.z != q.key.z || s.key.w != q.key.w) {
		vec3 now = vec3(0.0);
#if defined(OPTICS_BEAM_PASS)
		BeamPair pair = beam_means(q, LEAST_BATCH, moving_point, cuts, 0.0, 1.0, false, 1, true);
		now = to_linear_srgb(pair.fixed_estimate);
		early = to_linear_srgb(pair.moving);
		both = true;
#else
#ifndef OPTICS_SCREEN_PASS
		if (beamed(q)) {
			BeamPair pair = beam_means(q, LEAST_BATCH, moving_point, cuts, 0.0, 1.0, false, 1, true);
			now = to_linear_srgb(pair.fixed_estimate);
			early = to_linear_srgb(pair.moving);
			both = true;
		}
#endif
		if (!both) {
			now = batch(q, PROXY_POINTS, 0.5, cuts);
		}
#endif
		spent = spent + float(PROXY_POINTS + cuts.count);
		bool keep = false;
		bool plain = false;
		// Its rays go as they went, all of them one way, and what its batches found beyond the estimate is less
		// than an eye sees (Stage 33): a pixel that is its estimate. A change of colour leaves it so: what it
		// found is carried on in proportion to the light there is now, and it keeps its points. So a liquid that
		// changes colour at every step (a reaction) starts again only the histories of pixels whose rays part (a
		// rim shows more of the wall as its liquid darkens: that is found again), not every pixel of its vessel.
		plain = beam_held && beam_one_way && points >= SETTLED_POINTS && eye(lab_of(prox), lab_of(prox + d)) < 1.0;
		// (Stage 77) A pixel read by its own rays whose beam was traced again, its rays having moved (what lies in its
		// vessel changed on rows they pass: a pile's surface sweeping slowly across it), starts again: what its
		// corrections found was found along the rays it had, and kept by the drift rule (a small change of its
		// estimate, a frame at a time) they stood over its new estimate, its history settled, and nothing asked
		// for more: a slowly lowered pile left its old shape behind (the user's ghost, docs/TDD_77_GHOST.md).
		bool rays_moved = both && !beam_held;
		if (s.key.z == q.key.z && points > 0.0) {
			float moved = eye(lab_of(prox), lab_of(now));
			drift = drift + moved;
			keep = (drift <= DRIFT_MOST && !rays_moved) || plain;
			if (keep && plain) {
				float was = 0.2126 * prox.x + 0.7152 * prox.y + 0.0722 * prox.z;
				float is = 0.2126 * now.x + 0.7152 * now.y + 0.0722 * now.z;
				d = d * (was > 1.0e-6 ? clamp(is / was, 0.0, 4.0) : 1.0);
				drift = min(drift, DRIFT_MOST);
			} else if (keep) {
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
	// A pixel whose rays all go one way is nearly its estimate already; one whose rays part (a rim, a surface) is
	// not what it should be until its corrections come. Under a budget those have theirs first (Stage 33): the
	// first asks an eighth of what it wants, until its batches show a need. With asks to spare each has what it asks.
	if (kept_slot >= 0) {
		vec4 record = kept_vec(kept_slot);
		float kind = kept_vec(kept_slot + 1).w;
		// (not one whose batches have shown that it is not: a thin bright line across it, which its beam's
		// footprint misplaces a little, is found by its corrections alone, and it asks as a rim does)
		bool in_need = points >= 2.0 * float(LEAST_BATCH) && noise / (TARGET * TARGET) > points;
		if (record.w > 0.5 && record.z == floor(q.x) + 1920.0 * floor(q.y) && kind - (kind > 7.5 ? 8.0 : 0.0) > 3.5 && !in_need) {
			ask = ask * 0.125;
		}
	}
	float scaled = ask * scale;
	float chance = hash01(int(p.x), int(p.y), frame);
	// Whether it has a batch this frame, where the budget gives fewer than one to each: by its tile of the screen
	// (BATCH_TILE), not by itself. On the GPU pixels run in groups, each waiting for the one with most to do: batches
	// scattered pixel by pixel leave every group waiting, and a budget that halves the batches halves nothing.
	// Tile by tile, a group has batches or has none, and a frame costs what its budget allows (Stage 33). Each
	// pixel still has its batch as often.
	float turn = hash01(int(p.x) / BATCH_TILE_X + 7919, int(p.y) / BATCH_TILE_Y + 104729, frame);
	int n = scaled >= float(LEAST_BATCH) ? int(floor(scaled + chance)) : (turn < scaled / float(LEAST_BATCH) ? LEAST_BATCH : 0);
	want = ask;
	kept_whole = beam_held;
	if (both && kept_whole) {
		both = false; // its beam was kept: the batch, if it needs one, is read from the record
	}
	if (both) {
		// (a pixel whose rays have just moved is its beam's reading alone this frame: beam_means, Stage 33)
		n = LEAST_BATCH;
		if (beam_deferred) {
			n = 0;
		}
	}
	if (n > 0) {
		vec3 est = early;
		if (!both) {
			corrections_taken = 0;
			est = batch(q, n, moving_point, cuts);
		}
		spent = spent + float(n + cuts.count);
		// what the batch adds to the history: its bands, or, read by its own rays, the corrections it took
		float took = float(n);
		if (corrections_taken > 0) {
			took = float(corrections_taken);
		}
		if (points >= 1.0) {
			float dev = eye(lab_of(prox + d), lab_of(est));
			float trial = dev * dev / (1.0 / took + 1.0 / points);
			noise = frames >= 1.0 ? noise + (trial - noise) / min(frames + 1.0, 16.0) : trial;
		}
		d = (d * points + (est - prox) * took) / (points + took);
		points = min(points + took, 4096.0);
		frames = min(frames + 1.0, 256.0);
	}
	PixelState next;
	next.hist = vec4(d.x, d.y, d.z, points);
	next.key = q.key;
	next.prox = vec4(prox.x, prox.y, prox.z, drift);
	next.aux = vec4(noise, frames, want, spent);
	return next;
}

// ---- the debug infrared view (GAME_SPEC §12.4, Stage 46)
//
// The long-wave range at the eye for every pixel: a pixel's mean band signal (on the scale on which the room's is 1)
// is a temperature, the black body's that gives it, and the temperature a colour, one step from spectrum to RGB.

// the temperature of a signal: the long-wave light's table of it, 400 places spaced evenly in the signal's logarithm
// from 0.01 to 1000 of the room's (after its table the other way)
float infrared_kelvin(float signal) {
	float x = clamp(log(max(signal, 1.0e-6) / 0.01) / log(1.0e5) * 399.0, 0.0, 399.0);
	int i = min(int(floor(x)), 398);
	int base = LIGHT_SIZE + LIGHT_ENTRIES + LWIR_ROW;
	float a = light_vec(base + i / 4)[i - 4 * (i / 4)];
	float b = light_vec(base + (i + 1) / 4)[(i + 1) - 4 * ((i + 1) / 4)];
	return mix(a, b, x - float(i));
}

// a thermal camera's palette (an iron bow: black, violet, red, orange, yellow, white), linear sRGB, t from 0 to 1
vec3 infrared_palette(float t) {
	float x = clamp(t, 0.0, 1.0) * 5.0;
	int k = min(int(floor(x)), 4);
	float f = x - float(k);
	vec3 a = k == 0 ? vec3(0.0, 0.0, 0.0) : (k == 1 ? vec3(0.08, 0.0, 0.30) : (k == 2 ? vec3(0.55, 0.0, 0.25) : (k == 3 ? vec3(0.95, 0.20, 0.0) : vec3(1.0, 0.65, 0.0))));
	vec3 b = k == 0 ? vec3(0.08, 0.0, 0.30) : (k == 1 ? vec3(0.55, 0.0, 0.25) : (k == 2 ? vec3(0.95, 0.20, 0.0) : (k == 3 ? vec3(1.0, 0.65, 0.0) : vec3(1.0, 1.0, 1.0))));
	return mix(a, b, f);
}

// what the view shows of a signal: from 5 K under the room to 95 K over it
vec4 infrared_shown(float signal) {
	float t = (infrared_kelvin(signal) - (scene_vec(4).z - 5.0)) / 100.0;
	vec3 c = infrared_palette(t);
	return vec4(encode1(c.x), encode1(c.y), encode1(c.z), 1.0);
}

// ---- a sensor's display (GAME_SPEC §12.4, Stage 46): a thermal camera's screen
//
// Where a pixel looks at a sensor's display, what is in front of it is read as anything is, in the visible (the
// display's own face is a slab, lit by the room: its black glass); and at the display the reading goes on, in the
// long-wave range, at the matching point of the camera's frame, with the camera's own body hidden from it. The
// display shows its sensor pixel's mean band signal as its camera reads it, a temperature, a colour of its palette,
// its primaries driven to it, seen through what is in front. Each pixel of the display keeps a record in the pool
// (the includer's relay_vec and relay_put, at `relay_slot`; -1: none, and the display is dark):
// [0] the sensor pixel's mean signal (on the room's scale), its points, the sum of its squared deviations, the tag
// of what it read (what lies under the sensor pixel, the camera's settings); [1] to [3] its three primaries' light at
// full weight through what is in front, linear sRGB over the pixel, the tag of that front (w).
const int RELAY_HEAD_VECS = 5;
const int RELAY_PALETTE = 256;
const int RELAY_RECORD = 4;
const float RELAY_SETTLED = 128.0; // points before a sensor pixel may stop
const float RELAY_PRECISION = 1.0e-3; // the standard error of its mean signal it is brought to, relative (some 0.06 K)
const float RELAY_MOST = 1024.0; // and the most it reads (a sensor pixel across an edge: its places are spread evenly)
OPTICS_THREAD int relay_slot = -1;

int relay_block(int body) {
	return int(scene_vec(HEADER_VECS + body * BODY_VECS + 2).x);
}

// the record of the display's pixel at p in the pool, laid out by the display's rectangle on the screen (-1: none)
int relay_slot_at(int body, vec2 p) {
	int b = relay_block(body);
	vec4 pool = image_vec(b + 2);
	vec4 more = image_vec(b + 3);
	int x = int(floor(p.x)) - int(pool.y);
	int y = int(floor(p.y)) - int(pool.z);
	if (x < 0 || y < 0 || x >= int(pool.w) || y >= int(more.y)) {
		return -1;
	}
	return (int(pool.x) + y * int(pool.w) + x) * RELAY_RECORD;
}

// the sensor pixel the display's pixel at p shows: the frame's layout pixel matching the display's under p (its
// middle on the screen, and its width)
vec3 relay_sensor(int body, vec2 p) {
	vec4 rect = scene_vec(HEADER_VECS + body * BODY_VECS);
	int b = relay_block(body);
	vec4 h0 = image_vec(b);
	float px = image_vec(b + 1).w;
	float i = floor((p.x - rect.x) / px);
	float j = floor((p.y - rect.y) / px);
	return vec3(rect.x + h0.x + (i + 0.5) * px, rect.y + h0.y + (j + 0.5) * px, px);
}

// one point of the long-wave light at the place f of the room, at nm (the range already the long-wave's, the
// sensor's bodies hidden): what is there, read as any point is; the room's black body where there is nothing
float relay_point(vec2 f, float nm) {
	Path q = path_of(f, 1.0e-3);
	if (q.layers == 0) {
		return 1.0;
	}
	if (q.refractor >= 0 || q.ways) {
		RayMet m = ray_met(f, nm);
		vec2 t = ray_shade(m, nm);
		return t.x + t.y; // (the wall is the room's black body: 1)
	}
	return along(q, nm, 1.0, 0.0, 0.0, false, 1.0);
}

Layer layer_k(Path q, int k) {
	return k == 0 ? q.l0 : (k == 1 ? q.l1 : (k == 2 ? q.l2 : q.l3));
}

// what passes through the layers in front of the display at nm, over the pixel, of what its face sends: its share of
// the pixel times what each layer before it lets through
float relay_through(Path q, float nm) {
	Layer face = layer_k(q, q.relay_k);
	float a = face.solid;
	for (int k = 0; k < MAX_LAYERS; k++) {
		if (k <= q.relay_k || k >= q.layers) {
			continue;
		}
		Layer l = layer_k(q, k);
		int start = set_in(l.body);
		float per_cm = liquid_at(body_of(l.body), start, nm, q.y);
		float glass = exp(-row_at(start + 2 * ROW, nm) * l.wall);
		a = a * exp(-per_cm * (l.depth_cm + l.glowing)) * (1.0 - l.haze) * (1.0 - l.solid) * glass;
	}
	return a;
}

// the display's primary c's light at nm (a multiple of the room's light), at full weight
float relay_primary(int b, int c, float nm) {
	float x = clamp(nm - 380.0, 0.0, 400.0);
	int i = min(int(floor(x)), 399);
	int at = (b + RELAY_HEAD_VECS + RELAY_PALETTE) * 4 + c * ROW + i;
	float lo = image_vec(at / 4)[at - 4 * (at / 4)];
	float hi = image_vec((at + 1) / 4)[(at + 1) - 4 * ((at + 1) / 4)];
	return mix(lo, hi, x - float(i));
}

// One frame of a display's pixel's record (after its step: the pixel's own colour is its front's): its front's filter
// when that has changed, and points of its sensor pixel's long-wave light while it asks for them. What it asks for
// next (points).
float relay_step(Path q, vec2 p, int frame) {
	if (q.relay < 0 || relay_slot < 0) {
		return 0.0;
	}
	int b = relay_block(q.relay);
	vec4 h1 = image_vec(b + 1);
	vec4 h3 = image_vec(b + 3);
	// its front's filter, each primary's light through it, over the visible light's shares (64 bands, at their middles)
	float front_tag = q.key.x + 1.31 * q.key.y + 0.71 * q.key.z + 0.37 * q.key.w;
	if (relay_vec(relay_slot + 1).w != front_tag) {
		band_range = 0;
		vec3 xyz0 = vec3(0.0);
		vec3 xyz1 = vec3(0.0);
		vec3 xyz2 = vec3(0.0);
		for (int j = 0; j < 64; j++) {
			vec4 lo = share(float(j) / 64.0);
			vec4 hi = share(float(j + 1) / 64.0);
			float nm = share((float(j) + 0.5) / 64.0).x + 380.0;
			vec3 band = vec3(hi.y - lo.y, hi.z - lo.z, hi.w - lo.w) * relay_through(q, nm);
			xyz0 = xyz0 + band * relay_primary(b, 0, nm);
			xyz1 = xyz1 + band * relay_primary(b, 1, nm);
			xyz2 = xyz2 + band * relay_primary(b, 2, nm);
		}
		vec3 c0 = to_linear_srgb(xyz0);
		vec3 c1 = to_linear_srgb(xyz1);
		vec3 c2 = to_linear_srgb(xyz2);
		relay_put(relay_slot + 1, vec4(c0.x, c0.y, c0.z, front_tag));
		relay_put(relay_slot + 2, vec4(c1.x, c1.y, c1.z, front_tag));
		relay_put(relay_slot + 3, vec4(c2.x, c2.y, c2.z, front_tag));
	}
	// the sensor pixel it shows, and what lies under it there (its key in the long-wave range, its own bodies hidden)
	vec3 sensor = relay_sensor(q.relay, p);
	range_fixed = true;
	band_range = 1;
	ray_hide = int(h1.z);
	Path f = path_of(vec2(sensor.x, sensor.y), sensor.z);
	float tag = f.key.x * 1.37 + f.key.y * 0.71 + f.key.z * 0.13 + f.key.w * 0.29 + h3.x * 0.011 + sensor.x * 1.0e-3 + sensor.y * 7.0e-6;
	vec4 rec = relay_vec(relay_slot);
	if (rec.w != tag) {
		rec = vec4(0.0, 0.0, 0.0, tag);
	}
	float mean = rec.x;
	float points = rec.y;
	float m2 = rec.z;
	float want = points < RELAY_SETTLED ? RELAY_SETTLED - points : 0.0;
	if (points >= 2.0) {
		float aim = RELAY_PRECISION * max(mean, 1.0e-3);
		want = max(want, m2 / (points - 1.0) / (aim * aim) - points);
	}
	int n = want > 0.0 ? int(clamp(want, 16.0, 64.0)) : 0;
	if (points >= RELAY_MOST) {
		n = 0;
	}
	float seed = fract(0.7548776662 * p.x + 0.56984029 * p.y + 0.31 * float(frame % 97));
	for (int k = 0; k < 64; k++) {
		if (k >= n) {
			break;
		}
		float index = points + 1.0;
		// a place of the sensor pixel and a share of the long-wave light, each its own low-discrepancy sequence
		vec2 s = vec2(fract(seed + 0.7548776662 * index), fract(seed * 1.618034 + 0.5698402909 * index));
		float u = fract(seed * 2.414214 + 0.6180339887 * index);
		float nm = share(u).x + 380.0;
		float v = relay_point(vec2(sensor.x + (s.x - 0.5) * sensor.z, sensor.y + (s.y - 0.5) * sensor.z), nm);
		// the mean and the squared deviations, a point at a time (Welford)
		points = index;
		float d = v - mean;
		mean = mean + d / points;
		m2 = m2 + d * (v - mean);
	}
	range_fixed = false;
	ray_hide = 0;
	band_range = scene_vec(4).w > 0.5 ? 1 : 0;
	relay_put(relay_slot, vec4(mean, points, m2, tag));
	if (points >= RELAY_MOST) {
		return 0.0;
	}
	float more = points < RELAY_SETTLED ? RELAY_SETTLED - points : 0.0;
	if (points >= 2.0) {
		float aim = RELAY_PRECISION * max(mean, 1.0e-3);
		more = max(more, m2 / (points - 1.0) / (aim * aim) - points);
	}
	return max(more, 0.0);
}

// how much of each subpixel of the display's LCD pixel under p a sample `size` across about p covers, over the
// sample (the display's LCD pixels are its layout pixels; each subpixel a third across, its fill lit)
vec3 relay_stripes(int body, vec2 p, float size) {
	vec4 rect = scene_vec(HEADER_VECS + body * BODY_VECS);
	int b = relay_block(body);
	float px = image_vec(b + 1).w;
	vec4 h3 = image_vec(b + 3);
	float u0 = (p.x - 0.5 * size - rect.x) / px;
	float u1 = (p.x + 0.5 * size - rect.x) / px;
	float v0 = (p.y - 0.5 * size - rect.y) / px;
	float v1 = (p.y + 0.5 * size - rect.y) / px;
	float i = floor((p.x - rect.x) / px);
	float j = floor((p.y - rect.y) / px);
	float gap = (1.0 - h3.z) / 6.0;
	float down = max(min(v1, j + 0.5 * (1.0 + h3.w)) - max(v0, j + 0.5 * (1.0 - h3.w)), 0.0);
	float area = max((u1 - u0) * (v1 - v0), 1.0e-20);
	return vec3(max(min(u1, i + 1.0 / 3.0 - gap) - max(u0, i + gap), 0.0), max(min(u1, i + 2.0 / 3.0 - gap) - max(u0, i + 1.0 / 3.0 + gap), 0.0),
				   max(min(u1, i + 1.0 - gap) - max(u0, i + 2.0 / 3.0 + gap), 0.0)) * (down / area);
}

// what the display's primaries are driven to at p: its palette's colour at t (0 to RELAY_PALETTE - 1), or its overlay
// where that is (Stage 48: the camera's scale, numbers and marks), by how much of the LCD pixel under p it is
vec4 relay_drive(int body, float t, vec2 p) {
	int b = relay_block(body);
	int k = min(int(floor(t)), RELAY_PALETTE - 2);
	vec4 w = mix(image_vec(b + RELAY_HEAD_VECS + k), image_vec(b + RELAY_HEAD_VECS + k + 1), t - float(k));
	vec4 h4 = image_vec(b + 4);
	if (h4.z > 0.5) {
		vec4 rect = scene_vec(HEADER_VECS + body * BODY_VECS);
		float px = image_vec(b + 1).w;
		int i = clamp(int(floor((p.x - rect.x) / px)), 0, int(h4.x) - 1);
		int j = clamp(int(floor((p.y - rect.y) / px)), 0, int(h4.y) - 1);
		vec4 o = image_vec(int(h4.z) + j * int(h4.x) + i);
		w = mix(w, vec4(o.x, o.y, o.z, 0.0), o.w);
	}
	return w;
}

// what the display adds to the pixel: its primaries, driven to its palette's colour of its sensor pixel's temperature,
// through what is in front (linear sRGB); nothing until it has read
vec3 relay_light(Path q) {
	if (q.relay < 0 || relay_slot < 0) {
		return vec3(0.0);
	}
	vec4 rec = relay_vec(relay_slot);
	if (!(rec.y > 0.0)) {
		return vec3(0.0);
	}
	int b = relay_block(q.relay);
	vec4 h0 = image_vec(b);
	vec4 h1 = image_vec(b + 1);
	// the camera's temperature: the signal less the room it reflects, over its emissivity, as a black body's
	float own = (rec.x - (1.0 - h0.z) * h0.w) / max(h0.z, 1.0e-6);
	float t = clamp((infrared_kelvin(own) - h1.x) / max(h1.y - h1.x, 1.0e-6), 0.0, 1.0) * float(RELAY_PALETTE - 1);
	vec4 w = relay_drive(q.relay, t, vec2(q.x, q.y));
	vec3 st = relay_stripes(q.relay, vec2(q.x, q.y), q.size);
	vec4 r0 = relay_vec(relay_slot + 1);
	vec4 r1 = relay_vec(relay_slot + 2);
	vec4 r2 = relay_vec(relay_slot + 3);
	float red = w.x * st.x;
	float green = w.y * st.y;
	float blue = w.z * st.z;
	return vec3(r0.x * red + r1.x * green + r2.x * blue, r0.y * red + r1.y * green + r2.y * blue, r0.z * red + r1.z * green + r2.z * blue);
}

// A pixel no body covers shows the wall drawn there, its mean over the pixel: the room and the bodies are one
// picture at the screen's pixels (GAME_SPEC §12.3), which a host scales as one, so the wall seen through glass
// and beside it cannot part
// (Stage 61) within one of the screen's panels: a pixel there that sees only the wall is the screen's own drawing
bool in_panels(vec2 p) {
	for (int k = 6; k < 8; k++) {
		vec4 r = scene_vec(k);
		if (r.z > 0.0 && p.x >= r.x && p.x < r.x + r.z && p.y >= r.y && p.y < r.y + r.w) {
			return true;
		}
	}
	return false;
}

vec4 wall_shown(vec2 p, float size) {
	wall_base = 0; // (bare: the wall as drawn)
	if (in_panels(p)) {
		return vec4(0.0); // (the panel as the screen paints it: its background, its pictures)
	}
	if (scene_vec(4).w > 0.5) {
		return infrared_shown(1.0); // the room's own temperature (Stage 46)
	}
	int profile = wall_profile(p.y);
	float v = encode1(wall_span(profile, p.x - 0.5 * size, p.x + 0.5 * size));
	return vec4(v, v, v, 1.0);
}

// the state of a pixel that shows the wall: the wall table's own tag, so it is shown again when the wall changes
// (and matches no content: a body there starts its history)
vec4 wall_key() {
	wall_base = 0;
	return vec4(-1.0, wall_of(0).z, -1.0 - scene_vec(4).w, -1.0); // (and the range it is shown in: Stage 46)
}

// ---- a view's change (GAME_SPEC §12.3, Stage 42)
//
// While the view of the room moves (a zoom, a pan), the screen shows its pixels at the view it was last made at, the
// anchor, carried to the view now: nothing is traced. When it comes to rest, the scene is made at the new view and
// each pixel starts from the anchor's state carried to it, its content key's shape parts no longer its: so its first
// frame reads its fixed estimate again, and its history holds or starts again as for any change of content (its
// estimate moved by DRIFT_MOST or less: it keeps its corrections and some of its points). A view is (scale, x, y,
// 0): a point l of the layout is at l * scale + (x, y) on the screen.

// where the middle of pixel p of the view `now` is on the anchor's screen (x, y), and how many of the anchor's
// pixels across one of now's spans (z)
vec3 anchor_place(vec2 p, vec4 now, vec4 from) {
	float r = from.x / now.x;
	return vec3((p.x - now.y) * r + from.y, (p.y - now.z) * r + from.z, r);
}

// the anchor's pixel the k-th of a place's samples reads, and its weight: closer, the four pixels about it (between
// their middles); farther, sixteen points evenly over the pixels the place spans
vec3 anchor_sample(vec3 a, int k) {
	if (a.z <= 1.0) {
		float fx = a.x - 0.5;
		float fy = a.y - 0.5;
		float bx = floor(fx);
		float by = floor(fy);
		float tx = fx - bx;
		float ty = fy - by;
		int i = k - 2 * (k / 2);
		int j = k / 2;
		return vec3(bx + float(i), by + float(j), (i == 0 ? 1.0 - tx : tx) * (j == 0 ? 1.0 - ty : ty));
	}
	int i = k - 4 * (k / 4);
	int j = k / 4;
	return vec3(floor(a.x + a.z * ((float(i) + 0.5) / 4.0 - 0.5)), floor(a.y + a.z * ((float(j) + 0.5) / 4.0 - 0.5)), 1.0 / 16.0);
}

int anchor_samples(vec3 a) {
	return a.z <= 1.0 ? 4 : 16;
}

// what the anchor showed at a place (linear): the view's own (3), or the room's at 1x (4)
vec3 anchor_shown(vec3 a, int which) {
	vec3 c = vec3(0.0);
	for (int k = 0; k < 16; k++) {
		if (k >= anchor_samples(a)) {
			break;
		}
		vec3 at = anchor_sample(a, k);
		vec4 v = anchor_vec(which, int(at.x), int(at.y));
		c = c + vec3(v.x, v.y, v.z) * at.z;
	}
	return c;
}

// whether a place of the anchor's screen is within the part of it the anchor holds of the room, `room` (left, top,
// right, bottom): what a view further out shows of the room beyond it is not the anchor's
bool anchor_covers(vec3 a, vec4 room) {
	float half_span = 0.5 * max(a.z, 1.0);
	return a.x - half_span >= room.x && a.x + half_span <= room.z && a.y - half_span >= room.y && a.y + half_span <= room.w;
}

// The state a pixel of the view `now` starts from, carried from the anchor's (made at the view `from`): its fixed
// estimate and what its history adds, the mean of the anchor's there; its noise, their mean; its points, the fewest
// of theirs (none, where any of them had none: a pixel that was the wall, or had only just started; the drift rule
// then takes what its estimate moved by from them, as for any change of content); its
// frames, as many as settle; its content key the scene's colour key (key_z) and none of its shape key.
PixelState anchor_state(vec2 p, vec4 now, vec4 from, float key_z) {
	vec3 a = anchor_place(p, now, from);
	vec3 d = vec3(0.0);
	vec3 prox = vec3(0.0);
	float noise = 0.0;
	float points = 1.0e30;
	for (int k = 0; k < 16; k++) {
		if (k >= anchor_samples(a)) {
			break;
		}
		vec3 at = anchor_sample(a, k);
		vec4 h = anchor_vec(0, int(at.x), int(at.y));
		vec4 x = anchor_vec(1, int(at.x), int(at.y));
		vec4 u = anchor_vec(2, int(at.x), int(at.y));
		d = d + vec3(h.x, h.y, h.z) * at.z;
		prox = prox + vec3(x.x, x.y, x.z) * at.z;
		noise = noise + u.x * at.z;
		points = at.z > 0.0 ? min(points, h.w) : points;
	}
	PixelState s;
	points = points < 1.0 ? 0.0 : points;
	s.hist = vec4(d.x, d.y, d.z, points);
	s.prox = vec4(prox.x, prox.y, prox.z, 0.0);
	s.aux = vec4(noise, points > 0.0 ? SETTLED_FRAMES : 0.0, 0.0, 0.0);
	s.key = vec4(-2.0, -2.0, key_z, -2.0);
	return s;
}

// what the pixel shows: the fixed estimate and what its history adds, sRGB-encoded; where no body is, the wall
vec4 shown(Path q, PixelState s) {
	if (q.cover <= 0.0) {
		return wall_shown(vec2(q.x, q.y), q.size);
	}
	if (scene_vec(4).w > 0.5) {
		// the debug infrared view (Stage 46): the band signal's mean, its temperature, its colour
		return infrared_shown((s.prox.x + s.hist.x + s.prox.y + s.hist.y + s.prox.z + s.hist.z) / 3.0);
	}
	// (and a sensor's display, what it shows through what is in front of it: Stage 46)
	vec3 c = gamut(vec3(s.prox.x + s.hist.x, s.prox.y + s.hist.y, s.prox.z + s.hist.z) + relay_light(q));
	return vec4(encode1(c.x), encode1(c.y), encode1(c.z), 1.0);
}
