#include "lab/colour.hpp"

#include "chem/builtin_ids.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace lab {

using chem::optics::Bands;
using chem::optics::Spectrum;
using chem::optics::Xyz;

namespace {

constexpr double PI = 3.14159265358979323846;

double radians(double degrees) {
	return degrees * PI / 180.0;
}

const Xyz &white() {
	static const Xyz w = [] {
		Spectrum one;
		one.fill(1.0);
		return chem::optics::colour(one);
	}();
	return w;
}

double lab_f(double t) {
	constexpr double d = 6.0 / 29.0;
	return t > d * d * d ? std::cbrt(t) : t / (3.0 * d * d) + 4.0 / 29.0;
}

} // namespace

CieLab to_lab(const Xyz &xyz) {
	const double fx = lab_f(xyz.x / white().x), fy = lab_f(xyz.y / white().y), fz = lab_f(xyz.z / white().z);
	return CieLab{ 116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz) };
}

double difference(const CieLab &p, const CieLab &q) {
	return difference(p, q, 1.0, 1.0, 1.0);
}

double difference(const CieLab &p, const CieLab &q, double kl, double kc, double kh) {
	const double c1 = std::hypot(p.a, p.b), c2 = std::hypot(q.a, q.b);
	const double cbar7 = std::pow((c1 + c2) / 2.0, 7.0);
	const double g = 0.5 * (1.0 - std::sqrt(cbar7 / (cbar7 + std::pow(25.0, 7.0))));
	const double a1 = (1.0 + g) * p.a, a2 = (1.0 + g) * q.a;
	const double c1p = std::hypot(a1, p.b), c2p = std::hypot(a2, q.b);
	const auto hue = [](double b, double a) {
		if (a == 0.0 && b == 0.0) {
			return 0.0;
		}
		const double h = std::atan2(b, a) * 180.0 / PI;
		return h < 0.0 ? h + 360.0 : h;
	};
	const double h1 = hue(p.b, a1), h2 = hue(q.b, a2);
	const double dl = q.l - p.l, dc = c2p - c1p;
	double dh = 0.0;
	if (c1p * c2p != 0.0) {
		dh = h2 - h1;
		dh = dh > 180.0 ? dh - 360.0 : dh < -180.0 ? dh + 360.0 : dh;
	}
	const double dhh = 2.0 * std::sqrt(c1p * c2p) * std::sin(radians(dh / 2.0));
	const double lbar = (p.l + q.l) / 2.0, cbar = (c1p + c2p) / 2.0;
	double hbar = h1 + h2;
	if (c1p * c2p != 0.0) {
		hbar = std::fabs(h1 - h2) <= 180.0 ? (h1 + h2) / 2.0 : h1 + h2 < 360.0 ? (h1 + h2 + 360.0) / 2.0 : (h1 + h2 - 360.0) / 2.0;
	}
	const double t = 1.0 - 0.17 * std::cos(radians(hbar - 30.0)) + 0.24 * std::cos(radians(2.0 * hbar)) + 0.32 * std::cos(radians(3.0 * hbar + 6.0)) -
			0.20 * std::cos(radians(4.0 * hbar - 63.0));
	const double dtheta = 30.0 * std::exp(-((hbar - 275.0) / 25.0) * ((hbar - 275.0) / 25.0));
	const double cb7 = std::pow(cbar, 7.0);
	const double rc = 2.0 * std::sqrt(cb7 / (cb7 + std::pow(25.0, 7.0)));
	const double sl = 1.0 + 0.015 * (lbar - 50.0) * (lbar - 50.0) / std::sqrt(20.0 + (lbar - 50.0) * (lbar - 50.0));
	const double sc = 1.0 + 0.045 * cbar, sh = 1.0 + 0.015 * cbar * t;
	const double rt = -std::sin(radians(2.0 * dtheta)) * rc;
	const double l = dl / (kl * sl), c = dc / (kc * sc), h = dhh / (kh * sh);
	const double sum = l * l + c * c + h * h + rt * c * h;
	return std::sqrt(sum > 0.0 ? sum : 0.0);
}

double eye_difference(const CieLab &p, const CieLab &q) {
	const double base = difference(p, q, 0.5, 3.0, 2.0);
	const double neutral = 8.0 * std::fabs(std::log((std::hypot(q.a, q.b) + 8.0) / (std::hypot(p.a, p.b) + 8.0)));
	return std::sqrt(base * base + neutral * neutral);
}

namespace {

// OKLab (Ottosson 2020) from linear sRGB, and back
void to_oklab(const double rgb[3], double lab[3]) {
	const double l = std::cbrt(0.4122214708 * rgb[0] + 0.5363325363 * rgb[1] + 0.0514459929 * rgb[2]);
	const double m = std::cbrt(0.2119034982 * rgb[0] + 0.6806995451 * rgb[1] + 0.1073969566 * rgb[2]);
	const double s = std::cbrt(0.0883024619 * rgb[0] + 0.2817188376 * rgb[1] + 0.6299787005 * rgb[2]);
	lab[0] = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
	lab[1] = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
	lab[2] = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
}
void from_oklab(const double lab[3], double rgb[3]) {
	const double l = lab[0] + 0.3963377774 * lab[1] + 0.2158037573 * lab[2];
	const double m = lab[0] - 0.1055613458 * lab[1] - 0.0638541728 * lab[2];
	const double s = lab[0] - 0.0894841775 * lab[1] - 1.2914855480 * lab[2];
	const double l3 = l * l * l, m3 = m * m * m, s3 = s * s * s;
	rgb[0] = 4.0767416621 * l3 - 3.3077115913 * m3 + 0.2309699292 * s3;
	rgb[1] = -1.2684380046 * l3 + 2.6097574011 * m3 - 0.3413193965 * s3;
	rgb[2] = -0.0041960863 * l3 - 0.7034186147 * m3 + 1.7076147010 * s3;
}
bool in_gamut(const double rgb[3]) {
	constexpr double E = 1e-6;
	return rgb[0] >= -E && rgb[0] <= 1.0 + E && rgb[1] >= -E && rgb[1] <= 1.0 + E && rgb[2] >= -E && rgb[2] <= 1.0 + E;
}

} // namespace

void gamut_map(double rgb[3]) {
	// CSS Color 4: keep OKLab's lightness and hue, lower the chroma until clipping what is left is
	// within a just-noticeable difference (0.02 in OKLab); a fixed sixteen halvings, as the shader does
	if (in_gamut(rgb)) {
		for (int c = 0; c < 3; ++c) {
			rgb[c] = rgb[c] < 0.0 ? 0.0 : rgb[c] > 1.0 ? 1.0 : rgb[c];
		}
		return;
	}
	double lab[3];
	to_oklab(rgb, lab);
	if (lab[0] >= 1.0 || lab[0] <= 0.0) {
		const double v = lab[0] >= 1.0 ? 1.0 : 0.0;
		rgb[0] = rgb[1] = rgb[2] = v;
		return;
	}
	const double chroma = std::hypot(lab[1], lab[2]);
	double lo = 0.0, hi = chroma, clipped[3] = { lab[0], lab[0], lab[0] };
	for (int i = 0; i < 16; ++i) {
		const double mid = (lo + hi) / 2.0, scale = chroma > 0.0 ? mid / chroma : 0.0;
		const double at[3] = { lab[0], lab[1] * scale, lab[2] * scale };
		double candidate[3];
		from_oklab(at, candidate);
		for (int c = 0; c < 3; ++c) {
			clipped[c] = candidate[c] < 0.0 ? 0.0 : candidate[c] > 1.0 ? 1.0 : candidate[c];
		}
		double back[3];
		to_oklab(clipped, back);
		const double e = std::sqrt((back[0] - at[0]) * (back[0] - at[0]) + (back[1] - at[1]) * (back[1] - at[1]) + (back[2] - at[2]) * (back[2] - at[2]));
		(e < 0.02 ? lo : hi) = mid;
	}
	const double scale = chroma > 0.0 ? lo / chroma : 0.0, at[3] = { lab[0], lab[1] * scale, lab[2] * scale };
	from_oklab(at, rgb);
	for (int c = 0; c < 3; ++c) {
		rgb[c] = rgb[c] < 0.0 ? 0.0 : rgb[c] > 1.0 ? 1.0 : rgb[c];
	}
}

CieLab shown(const Xyz &xyz, int code[3]) {
	double rgb[3];
	to_linear_srgb(xyz, rgb);
	gamut_map(rgb);
	double lin[3];
	for (int c = 0; c < 3; ++c) {
		const int v = static_cast<int>(std::lround(srgb_encode(rgb[c]) * 255.0));
		if (code != nullptr) {
			code[c] = v;
		}
		const double e = v / 255.0;
		lin[c] = e <= 0.04045 ? e / 12.92 : std::pow((e + 0.055) / 1.055, 2.4);
	}
	return to_lab(Xyz{ 0.4124564 * lin[0] + 0.3575761 * lin[1] + 0.1804375 * lin[2], 0.2126729 * lin[0] + 0.7151522 * lin[1] + 0.0721750 * lin[2],
			0.0193339 * lin[0] + 0.1191920 * lin[1] + 0.9503041 * lin[2] });
}

void to_linear_srgb(const Xyz &xyz, double rgb[3]) {
	// IEC 61966-2-1, D65
	rgb[0] = 3.2404542 * xyz.x - 1.5371385 * xyz.y - 0.4985314 * xyz.z;
	rgb[1] = -0.9692660 * xyz.x + 1.8760108 * xyz.y + 0.0415560 * xyz.z;
	rgb[2] = 0.0556434 * xyz.x - 0.2040259 * xyz.y + 1.0572252 * xyz.z;
}

double srgb_encode(double linear) {
	const double v = linear < 0.0 ? 0.0 : linear > 1.0 ? 1.0 : linear;
	return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

namespace {

constexpr double LN10 = 2.302585092994046;

// One thing light meets on its way to the eye.
struct Layer {
	enum What { Absorb, Reflect, Wall } what;
	const Spectrum *spectrum;
	double amount; // Absorb: mol/L * cm. Wall: thicknesses. Reflect: unused
};

// what is linear before the exponential is what a band carries: an optical
// depth per unit amount, or a reflectance
Spectrum linear_of(const Layer &layer) {
	Spectrum out;
	for (size_t i = 0; i < out.size(); ++i) {
		const double v = (*layer.spectrum)[i];
		out[i] = layer.what == Layer::Absorb ? v * LN10 : layer.what == Layer::Wall ? -std::log(v) : v;
	}
	return out;
}

// One colour of the test set, with its reference.
struct Sample {
	std::string name;
	std::vector<Layer> layers; // or:
	bool glows = false;
	Spectrum glow{};
	double exposure = 1.0;
	CieLab reference;
};

// a glowing body: Planck's law, on an arbitrary scale
Spectrum blackbody(double kelvin) {
	Spectrum s;
	for (size_t i = 0; i < s.size(); ++i) {
		const double metres = (380.0 + static_cast<double>(i)) * 1e-9;
		s[i] = 1e-30 / (std::pow(metres, 5.0) * (std::exp(0.014387769 / (metres * kelvin)) - 1.0));
	}
	return s;
}

CieLab reference_of(const std::vector<Layer> &layers) {
	Spectrum factor;
	factor.fill(1.0);
	for (const Layer &layer : layers) {
		const Spectrum linear = linear_of(layer);
		for (size_t i = 0; i < factor.size(); ++i) {
			factor[i] *= layer.what == Layer::Reflect ? linear[i] : std::exp(-linear[i] * layer.amount);
		}
	}
	return to_lab(chem::optics::colour(factor));
}

std::string label(const char *what, double a, const char *unit_a, double b = 0.0, const char *unit_b = nullptr) {
	char text[160];
	if (unit_b != nullptr) {
		std::snprintf(text, sizeof text, "%s, %g %s, %g %s", what, a, unit_a, b, unit_b);
	} else {
		std::snprintf(text, sizeof text, "%s, %g %s", what, a, unit_a);
	}
	return text;
}

// every colour the demo can show
const std::vector<Sample> &samples() {
	static const std::vector<Sample> all = [] {
		using namespace chem;
		const Spectrum &copper = optics::spectrum(builtin::copper_sulfate_aq);
		const Spectrum &latex = optics::spectrum(builtin::latex);
		const struct {
			const char *name;
			const Spectrum *reflectance;
		} solids[] = { { "malachite", &optics::spectrum(builtin::malachite) }, { "copper oxide", &optics::spectrum(builtin::copper_oxide) },
			{ "pentahydrate", &optics::spectrum(builtin::copper_sulfate_pentahydrate) }, { "anhydrous salt", &optics::spectrum(builtin::copper_sulfate) } };
		std::vector<Sample> out;
		const auto add = [&](std::string name, std::vector<Layer> layers) {
			Sample s;
			s.name = std::move(name);
			s.reference = reference_of(layers);
			s.layers = std::move(layers);
			out.push_back(std::move(s));
		};
		for (const double molar : { 0.05, 0.1, 0.2, 0.45, 0.8, 1.3, 2.0, 3.0, 4.7 }) {
			for (const double cm : { 0.5, 1.0, 2.0, 4.0, 6.0, 10.0 }) {
				add(label("copper sulfate solution", molar, "mol/L", cm, "cm"), { { Layer::Absorb, &copper, molar * cm } });
			}
		}
		for (const auto &solid : solids) {
			add(solid.name, { { Layer::Reflect, solid.reflectance, 0.0 } });
			for (const double molar : { 0.45, 1.3, 3.0 }) {
				for (const double cm : { 1.0, 3.0 }) { // seen through the solution, there and back
					add(label((std::string(solid.name) + " under solution").c_str(), molar, "mol/L", cm, "cm"),
							{ { Layer::Reflect, solid.reflectance, 0.0 }, { Layer::Absorb, &copper, molar * 2.0 * cm } });
				}
			}
		}
		for (const double thick : { 0.25, 0.5, 1.0, 2.0, 4.0 }) {
			add(label("the balloon's rubber", thick, "thicknesses"), { { Layer::Wall, &latex, thick } });
			add(label("solution (0.45 mol/L, 6 cm) behind the rubber", thick, "thicknesses"), { { Layer::Wall, &latex, thick }, { Layer::Absorb, &copper, 0.45 * 6.0 } });
		}
		// glowing things, exposed so that each reference is a mid grey's lightness
		for (double kelvin = 800.0; kelvin <= 2500.0; kelvin += 100.0) {
			Sample s;
			s.name = label("a glowing body", kelvin, "K");
			s.glows = true;
			s.glow = blackbody(kelvin);
			const Xyz xyz = optics::colour_emitted(s.glow);
			s.exposure = 0.18 / xyz.y;
			s.reference = to_lab(Xyz{ xyz.x * s.exposure, xyz.y * s.exposure, xyz.z * s.exposure });
			out.push_back(std::move(s));
		}
		return out;
	}();
	return all;
}

// where bands degrade: absorption lines narrower than a band (none in the demo)
struct Lines {
	std::vector<Spectrum> line;
	std::vector<Sample> sample;
};
const Lines &lines() {
	static const Lines all = [] {
		Lines out;
		out.line.reserve(16);
		for (double centre = 400.0; centre <= 700.0; centre += 25.0) {
			Spectrum line; // the molar absorptivity of a line 10 nm wide, peak 1
			for (size_t i = 0; i < line.size(); ++i) {
				const double d = (380.0 + static_cast<double>(i) - centre) / 10.0;
				line[i] = std::exp(-4.0 * std::log(2.0) * d * d);
			}
			out.line.push_back(line);
			for (const double depth : { 1.0, 3.0 }) {
				Sample s;
				s.name = label("a 10 nm line", centre, "nm", depth, "absorbance");
				s.layers = { { Layer::Absorb, &out.line.back(), depth } };
				s.reference = reference_of(s.layers);
				out.sample.push_back(std::move(s));
			}
		}
		return out;
	}();
	return all;
}

// the weight of each nanometre, as the bands weigh it
double weight(size_t i) {
	using namespace chem::optics;
	return (xbar()[i] + ybar()[i] + zbar()[i]) * d65()[i];
}

// The slope of a spectrum across each band, as the difference between the band's
// two ends: a weighted least-squares line, over the even band of the same spread.
void band_tilts(const Bands &bands, const Spectrum &s, std::span<double> out) {
	for (int k = 0; k < bands.count; ++k) {
		const int from = bands.edge[static_cast<size_t>(k)], to = bands.edge[static_cast<size_t>(k) + 1];
		double w = 0.0, wx = 0.0, ws = 0.0;
		for (int i = from; i < to; ++i) {
			const auto u = static_cast<size_t>(i);
			w += weight(u);
			wx += weight(u) * i;
			ws += weight(u) * s[u];
		}
		double tilt = 0.0;
		if (w > 0.0) {
			const double x0 = wx / w, s0 = ws / w;
			double var = 0.0, cov = 0.0;
			for (int i = from; i < to; ++i) {
				const auto u = static_cast<size_t>(i);
				var += weight(u) * (i - x0) * (i - x0);
				cov += weight(u) * (i - x0) * (s[u] - s0);
			}
			tilt = var > 0.0 ? cov / var * std::sqrt(12.0 * var / w) : 0.0;
		}
		out[static_cast<size_t>(k)] = tilt;
	}
}

CieLab banded(const Bands &bands, BandModel model, const Sample &sample) {
	using namespace chem::optics;
	const auto n = static_cast<size_t>(bands.count);
	std::array<double, MAX_BANDS> average{};
	if (sample.glows) {
		band_averages(bands, sample.glow, average, true);
		const Xyz xyz = colour_emitted(bands, std::span<const double>(average.data(), n));
		return to_lab(Xyz{ xyz.x * sample.exposure, xyz.y * sample.exposure, xyz.z * sample.exposure });
	}
	std::array<double, MAX_BANDS> reflect, depth{}, tilt{}, slope{};
	reflect.fill(1.0);
	for (const Layer &layer : sample.layers) {
		const Spectrum linear = linear_of(layer);
		band_averages(bands, linear, average);
		if (layer.what == Layer::Reflect) {
			for (size_t k = 0; k < n; ++k) {
				reflect[k] *= average[k];
			}
			continue;
		}
		band_tilts(bands, linear, slope);
		for (size_t k = 0; k < n; ++k) {
			depth[k] += average[k] * layer.amount;
			tilt[k] += slope[k] * layer.amount;
		}
	}
	std::array<double, MAX_BANDS> factor{};
	for (size_t k = 0; k < n; ++k) {
		// a band dimmed unevenly lets more through than its average depth says: sinh(u)/u
		const double u = model == BandModel::Tilted ? std::fabs(tilt[k]) / 2.0 : 0.0;
		const double through = u < 1e-4 ? std::exp(-depth[k]) * (1.0 + u * u / 6.0) : (std::exp(u - depth[k]) - std::exp(-u - depth[k])) / (2.0 * u);
		factor[k] = reflect[k] * through;
	}
	return to_lab(colour(bands, std::span<const double>(factor.data(), n)));
}

} // namespace

BandError band_error(const Bands &bands, BandModel model, bool detail) {
	BandError out;
	double sum = 0.0;
	for (const Sample &s : samples()) {
		const double e = difference(s.reference, banded(bands, model, s));
		sum += e;
		if (e > out.worst) {
			out.worst = e;
			out.worst_case = s.name;
		}
		if (detail) {
			out.all.emplace_back(e, s.name);
		}
	}
	out.mean = sum / static_cast<double>(samples().size());
	if (detail) {
		for (const Sample &s : lines().sample) {
			const double e = difference(s.reference, banded(bands, model, s));
			out.worst_narrow = e > out.worst_narrow ? e : out.worst_narrow;
		}
	}
	return out;
}

} // namespace lab
