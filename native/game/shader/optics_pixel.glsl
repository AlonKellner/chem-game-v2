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
//   vec4 light_vec(int i)    the room light's energy table: nm - 380, and its colour up to there, X Y Z

const int HEADER_VECS = 3;
const int BODY_VECS = 7;
const int SHAPE_BASE = 3 + 16 * 7;
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
	float rd; // the view ray's distance from its axis (cm), signed
	float rr; // its outer radius (cm)
	float rt; // its glass's thickness (cm)
	float raxis; // its axis on the screen (pixels)
	float rcm; // centimetres a pixel
	bool rliquid; // whether the ray meets its liquid (else air)
	float rcover; // how much of the pixel it covers: what its surfaces turn away is in proportion
};

// a pixel's state between frames
struct PixelState {
	vec4 hist; // what the moving points add to the fixed estimate (linear sRGB, averaged), and the points behind it
	vec4 key; // the content key it belongs to
	vec4 prox; // the fixed estimate (linear sRGB), and how far it has moved since the history began
	vec4 aux; // the noise of one point (eye metric, squared), frames in the history, points asked for, points spent
};

// ---- where the pixel looks

Path path_of(vec2 p) {
	Path q;
	q.layers = 0;
	q.cover = 0.0;
	q.glow = vec3(0.0);
	q.key = vec4(0.0);
	q.x = p.x;
	q.y = p.y;
	q.refractor = -1;
	q.rd = 0.0;
	q.rr = 1.0;
	q.rt = 0.0;
	q.raxis = 0.0;
	q.rcm = 1.0;
	q.rliquid = false;
	q.rcover = 0.0;
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
		if (form.x < 0.5) {
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
		float cover = clamp(within + 0.5, 0.0, 1.0);
		if (cover <= 0.0 || q.layers >= MAX_LAYERS) {
			continue;
		}
		float cm = 2.0 * radius * chord * form.y;
		// a pixel half under the surface is half as deep in it: T^coverage
		float wet = clamp(p.y - form.z + 0.5, 0.0, 1.0) * cover;
		float heap = form.w * max(1.0 - abs(from_axis) / max(more.z, 0.001), 0.0);
		Layer l;
		l.body = i;
		l.depth_cm = cm * wet;
		l.haze = 1.0 - exp(-more.x * cm * wet);
		l.solid = form.w > 0.0 ? clamp(p.y - (ground - heap) + 0.5, 0.0, 1.0) * cover : 0.0;
		l.wall = wall * cover;
		l.wet = wet;
		vec4 bend = scene_vec(at + 5);
		if (bend.w > 0.5 && q.refractor < 0) {
			// a vessel bends the view: the first one the pixel looks through
			q.refractor = i;
			q.rd = from_axis * form.y;
			q.rr = radius * form.y;
			q.rt = bend.x;
			q.raxis = rect.x + radius;
			q.rcm = form.y;
			q.rliquid = wet >= 0.5 * cover;
			q.rcover = cover;
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
		vec4 glow = scene_vec(at + 3);
		q.glow = mix(q.glow, vec3(glow.x, glow.y, glow.z), l.solid);
		q.cover = max(q.cover, cover);
		vec4 ids = scene_vec(at + 4);
		float w = 1.0 + 0.6180339887 * float(i);
		q.key = q.key + vec4((l.depth_cm + 3.0 * l.haze) * w, (l.solid + 7.0 * l.wall) * w, ids.x, ids.y * w);
	}
	// what glows is shown as it is, outside the history; it is in the key so that a pixel shows it anew
	q.key.y = q.key.y + 13.0 * q.back + 0.001 * (q.glow.x + 2.0 * q.glow.y + 3.0 * q.glow.z);
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

// the unpolarised reflectance of a surface of radius r between indices n1 and n2, for a ray d from the axis
// (its sine on each side is d / (n r)); 1 where it cannot pass
float fresnel(float n1, float n2, float d, float r) {
	if (n1 == n2) {
		return 0.0; // no surface at all
	}
	float s1 = d / (n1 * r);
	float s2 = d / (n2 * r);
	if (s1 >= 1.0 || s2 >= 1.0) {
		return 1.0;
	}
	float c1 = sqrt(1.0 - s1 * s1);
	float c2 = sqrt(1.0 - s2 * s2);
	float rs = (n1 * c1 - n2 * c2) / (n1 * c1 + n2 * c2);
	float rp = (n1 * c2 - n2 * c1) / (n1 * c2 + n2 * c1);
	return 0.5 * (rs * rs + rp * rp);
}

// the wall at (x, y): its radiance on the bench or off it, and the shapes drawn on it that a body's rays reach
float backdrop(float x, float y, int body) {
	vec4 head = scene_vec(0);
	vec4 bench = scene_vec(1);
	float seen = x >= bench.x && x < bench.x + bench.z && y >= bench.y && y < bench.y + bench.w ? head.y : head.z;
	vec4 bend = scene_vec(HEADER_VECS + body * BODY_VECS + 5);
	int first = int(bend.y);
	int count = int(bend.z);
	for (int k = 0; k < 32; k++) {
		if (k >= count) {
			break;
		}
		vec4 r = scene_vec(SHAPE_BASE + 2 * (first + k));
		if (x >= r.x && x < r.x + r.z && y >= r.y && y < r.y + r.w) {
			seen = scene_vec(SHAPE_BASE + 2 * (first + k) + 1).x;
		}
	}
	return seen;
}

// what a vessel does to the view at nm (GAME_SPEC §12.3): where the bent ray meets the wall (x, on the
// screen), what all its surfaces let through (pass, Fresnel), and the chord of its liquid the ray crosses (cm)
struct Bend {
	float x;
	float pass;
	float chord;
};

Bend bend_at(Path q, float nm) {
	Bend b;
	b.x = q.x;
	b.pass = 1.0;
	b.chord = 0.0;
	if (q.refractor < 0) {
		return b;
	}
	vec4 head = scene_vec(2);
	int start = q.refractor * ROWS_PER_BODY * ROW;
	float n_glass = row_at(start + 4 * ROW, nm);
	float n_in = q.rliquid ? row_at(start + 3 * ROW, nm) : 1.0;
	float d = abs(q.rd);
	float ri = q.rr - q.rt;
	float n_first = q.rt > 0.0 ? n_glass : n_in;
	// in through the outer surface; on to the inner one if the ray reaches it and is not turned back there
	float turn = asin(min(d / q.rr, 1.0)) - asin(min(d / (n_first * q.rr), 1.0));
	float r_outer = fresnel(1.0, n_first, d, q.rr);
	float r_inner = 0.0;
	bool inner = q.rt > 0.0 && d < n_glass * ri && d < n_in * ri;
	if (inner) {
		turn = turn + asin(d / (n_glass * ri)) - asin(d / (n_in * ri));
		r_inner = fresnel(n_glass, n_in, d, ri);
	}
	turn = 2.0 * turn;
	b.pass = 1.0 - q.rcover * (1.0 - (1.0 - r_outer) * (1.0 - r_outer) * (1.0 - r_inner) * (1.0 - r_inner));
	float r_in = q.rt > 0.0 ? ri : q.rr;
	if (q.rliquid && (q.rt <= 0.0 || inner) && d < n_in * r_in) {
		b.chord = 2.0 * sqrt(r_in * r_in - (d / n_in) * (d / n_in));
	}
	// it leaves on a line d from the axis, turned by `turn`, and meets the wall D behind the axis
	float c = cos(turn);
	float landed = c > 0.05 ? (d - head.x * sin(turn)) / c : -100000.0;
	b.x = q.raxis + (q.rd < 0.0 ? -landed : landed) / q.rcm;
	return b;
}

// the share of the room's light at nm that reaches the eye: off the backdrop, through every body in front of it;
// a vessel bends the view: the wall is read where the bent ray lands, its liquid along the ray's own chord, and
// what its surfaces turn away shows the room in front
float through(Path q, float nm) {
	float seen = q.back;
	Bend b = bend_at(q, nm);
	float chord = b.chord;
	float pass = b.pass;
	if (q.refractor >= 0) {
		seen = backdrop(b.x, q.y, q.refractor);
	}
	if (q.layers > 0) {
		seen = through_layer(q.l0, seen, nm, q.l0.body == q.refractor ? chord * q.l0.wet : q.l0.depth_cm);
	}
	if (q.layers > 1) {
		seen = through_layer(q.l1, seen, nm, q.l1.body == q.refractor ? chord * q.l1.wet : q.l1.depth_cm);
	}
	if (q.layers > 2) {
		seen = through_layer(q.l2, seen, nm, q.l2.body == q.refractor ? chord * q.l2.wet : q.l2.depth_cm);
	}
	if (q.layers > 3) {
		seen = through_layer(q.l3, seen, nm, q.l3.body == q.refractor ? chord * q.l3.wet : q.l3.depth_cm);
	}
	if (q.refractor >= 0) {
		seen = pass * seen + (1.0 - pass) * scene_vec(2).y;
	}
	return seen;
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

// n bands, each its exact colour times what gets through at one point in it; offset in [0, 1) places the points
vec3 batch(Path q, int n, float offset) {
	vec3 sum = vec3(0.0);
	vec4 before = share(0.0);
	for (int b = 0; b < 64; b++) {
		if (b >= n) {
			break;
		}
		vec4 upto = share(float(b + 1) / float(n));
		float nm = share((float(b) + offset) / float(n)).x + 380.0;
		sum = sum + vec3(upto.y - before.y, upto.z - before.z, upto.w - before.w) * through(q, nm);
		before = upto;
	}
	return to_linear_srgb(sum);
}

// the colour itself: the light's shares, each its own band, read at its middle
vec3 truth(Path q, int shares) {
	vec3 sum = vec3(0.0);
	for (int j = 0; j < 4096; j++) {
		if (j >= shares) {
			break;
		}
		vec4 a = share(float(j) / float(shares));
		vec4 b = share(float(j + 1) / float(shares));
		float nm = share((float(j) + 0.5) / float(shares)).x + 380.0;
		sum = sum + vec3(b.y - a.y, b.z - a.z, b.w - a.w) * through(q, nm);
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
	if (s.key.x != q.key.x || s.key.y != q.key.y || s.key.z != q.key.z || s.key.w != q.key.w) {
		vec3 now = batch(q, PROXY_POINTS, 0.5);
		spent = spent + float(PROXY_POINTS);
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
		vec3 est = batch(q, n, fract(seed + 0.6180339887 * float(frame % 8192)));
		spent = spent + float(n);
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
