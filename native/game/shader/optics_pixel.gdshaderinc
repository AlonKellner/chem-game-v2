// What one pixel of the bench looks like (GAME_SPEC §12): the light of the
// backdrop carried through every body in front of it, band by band, then one
// conversion to a colour.
//
// This file is the renderer. Godot compiles it as a shader; the lab layer
// compiles the same text as C++ (native/game/src/optics_reference.cpp), and
// that build is the reference the GPU's picture is held to (O4). So it keeps
// to what both languages read alike: no swizzles, no out parameters, no
// constructors but vec2(a, b), vec4(a) and vec4(a, b, c, d).
//
// The scene is one table of vec4s (lab/optics_scene.hpp lays it out):
//   0            bodies, the backdrop's radiance on the bench, off it, unused
//   1            the bench's rectangle: x, y, w, h
//   2 .. 25      each band's colour in linear sRGB under the room's light
//   26 + 22 i    body i:
//     +0         its rectangle on the screen, in pixels
//     +1         shape (0 a cylinder, 1 a ball), centimetres to a pixel, the liquid's surface (y), the heap's height (pixels)
//     +2         suspended particles' extinction (1/cm), one wall's thickness (reference thicknesses), the heap's half width (pixels), unused
//     +3         what its solids emit, linear sRGB
//     +4 .. 9    the liquid's optical depth per centimetre, by band
//     +10 .. 15  the solids' reflectance, by band
//     +16 .. 21  the wall's optical depth at its reference thickness, by band

const int BAND_VECS = 6; // 24 bands, four to a vec4
const int HEADER_VECS = 26;
const int BODY_VECS = 22;
const float WALL_MOST = 4.0; // a wall seen edge on is no thicker than this, in reference thicknesses

uniform vec4 scene[378];

struct Light {
	vec4 band[6]; // radiance by band; the room's white is 1
	vec4 glow; // emitted light, linear sRGB
	float cover; // how much of the pixel a body covers
};

Light transport(vec2 p) {
	Light l;
	bool on_bench = p.x >= scene[1].x && p.x < scene[1].x + scene[1].z && p.y >= scene[1].y && p.y < scene[1].y + scene[1].w;
	float back = on_bench ? scene[0].y : scene[0].z;
	for (int j = 0; j < BAND_VECS; j++) {
		l.band[j] = vec4(back);
	}
	l.glow = vec4(0.0);
	l.cover = 0.0;
	int count = int(scene[0].x);
	for (int i = 0; i < count; i++) {
		int at = HEADER_VECS + i * BODY_VECS;
		vec4 rect = scene[at];
		if (p.x < rect.x - 1.0 || p.x > rect.x + rect.z + 1.0 || p.y < rect.y - 1.0 || p.y > rect.y + rect.w + 1.0) {
			continue;
		}
		vec4 form = scene[at + 1];
		vec4 more = scene[at + 2];
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
		if (cover <= 0.0) {
			continue;
		}
		float cm = 2.0 * radius * chord * form.y;
		// a pixel half under the surface is half as deep in it: T^coverage
		float wet = clamp(p.y - form.z + 0.5, 0.0, 1.0) * cover;
		float heap = form.w * max(1.0 - abs(from_axis) / max(more.z, 0.001), 0.0);
		float solid = form.w > 0.0 ? clamp(p.y - (ground - heap) + 0.5, 0.0, 1.0) * cover : 0.0;
		float haze = 1.0 - exp(-more.x * cm * wet);
		for (int j = 0; j < BAND_VECS; j++) {
			vec4 depth = scene[at + 4 + j] * (cm * wet);
			vec4 colour = scene[at + 10 + j];
			// through the liquid (Beer-Lambert)
			vec4 seen = l.band[j] * exp(-depth);
			// what is suspended in it turns the room's light back, from half way in on average
			seen = mix(seen, colour * exp(-depth * 0.5), haze);
			// what lies in it is lit and seen through it: there and back is one chord
			seen = mix(seen, colour * exp(-depth), solid);
			// and the wall
			l.band[j] = seen * exp(-scene[at + 16 + j] * (wall * cover));
		}
		l.glow = mix(l.glow, scene[at + 3], solid);
		l.cover = max(l.cover, cover);
	}
	return l;
}

// the sRGB transfer function
float encode(float v) {
	return v <= 0.0031308 ? 12.92 * v : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}

// The light as a colour, sRGB-encoded; alpha 0 where no body is.
vec4 colour_of(Light l) {
	vec4 rgb = l.glow;
	for (int j = 0; j < BAND_VECS; j++) {
		int k = 2 + 4 * j;
		rgb = rgb + scene[k] * l.band[j].x + scene[k + 1] * l.band[j].y + scene[k + 2] * l.band[j].z + scene[k + 3] * l.band[j].w;
	}
	rgb = clamp(rgb, vec4(0.0), vec4(1.0)); // fixed exposure; a colour no display has is clipped
	return vec4(encode(rgb.x), encode(rgb.y), encode(rgb.z), l.cover > 0.0 ? 1.0 : 0.0);
}

vec4 shade(vec2 p) {
	return colour_of(transport(p));
}
