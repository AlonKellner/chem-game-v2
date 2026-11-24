#include "chem/optics.hpp"

#include "chem/math.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace chem::optics {

namespace {

// the weight of each nanometre: what the eye and the illuminant make of it
double weight(int i) {
	return (xbar()[static_cast<size_t>(i)] + ybar()[static_cast<size_t>(i)] + zbar()[static_cast<size_t>(i)]) * d65()[static_cast<size_t>(i)];
}

// the illuminant's own Y: the scale on which its white is 1
double white() {
	static const double y = [] {
		double sum = 0.0;
		for (size_t i = 0; i < COUNT; ++i) {
			sum += ybar()[i] * d65()[i];
		}
		return sum;
	}();
	return y;
}

Bands finish(Bands bands) {
	for (int k = 0; k < bands.count; ++k) {
		double total = 0.0, moment = 0.0;
		Xyz lit, plain;
		for (int i = bands.edge[static_cast<size_t>(k)]; i < bands.edge[static_cast<size_t>(k) + 1]; ++i) {
			const auto u = static_cast<size_t>(i);
			lit.x += xbar()[u] * d65()[u];
			lit.y += ybar()[u] * d65()[u];
			lit.z += zbar()[u] * d65()[u];
			plain.x += xbar()[u];
			plain.y += ybar()[u];
			plain.z += zbar()[u];
			total += weight(i);
			moment += weight(i) * static_cast<double>(FIRST_NM + i);
		}
		const double w = white();
		bands.lit[static_cast<size_t>(k)] = Xyz{ lit.x / w, lit.y / w, lit.z / w };
		bands.plain[static_cast<size_t>(k)] = Xyz{ plain.x / w, plain.y / w, plain.z / w };
		bands.centre_nm[static_cast<size_t>(k)] = total > 0.0 ? moment / total : 0.0;
	}
	return bands;
}

} // namespace

Xyz colour(const Spectrum &factor) {
	Xyz out;
	for (size_t i = 0; i < COUNT; ++i) {
		out.x += xbar()[i] * d65()[i] * factor[i];
		out.y += ybar()[i] * d65()[i] * factor[i];
		out.z += zbar()[i] * d65()[i] * factor[i];
	}
	const double w = white();
	return Xyz{ out.x / w, out.y / w, out.z / w };
}

Xyz colour_emitted(const Spectrum &radiance) {
	Xyz out;
	for (size_t i = 0; i < COUNT; ++i) {
		out.x += xbar()[i] * radiance[i];
		out.y += ybar()[i] * radiance[i];
		out.z += zbar()[i] * radiance[i];
	}
	const double w = white();
	return Xyz{ out.x / w, out.y / w, out.z / w };
}

Bands bands_uniform(int count, int from_nm, int to_nm) {
	Bands bands;
	bands.count = count < 1 ? 1 : count > MAX_BANDS ? MAX_BANDS : count;
	const int lo = from_nm - FIRST_NM, hi = to_nm - FIRST_NM + 1;
	for (int k = 0; k <= bands.count; ++k) {
		bands.edge[static_cast<size_t>(k)] = lo + (hi - lo) * k / bands.count;
	}
	bands.edge[0] = 0; // the tails go to the end bands
	bands.edge[static_cast<size_t>(bands.count)] = COUNT;
	return finish(bands);
}

Bands bands_equal_weight(int count) {
	Bands bands;
	bands.count = count < 1 ? 1 : count > MAX_BANDS ? MAX_BANDS : count;
	double total = 0.0;
	for (int i = 0; i < COUNT; ++i) {
		total += weight(i);
	}
	double running = 0.0;
	int next = 1;
	bands.edge[0] = 0;
	for (int i = 0; i < COUNT && next < bands.count; ++i) {
		running += weight(i);
		if (running >= total * static_cast<double>(next) / static_cast<double>(bands.count)) {
			bands.edge[static_cast<size_t>(next++)] = i + 1;
		}
	}
	for (; next < bands.count; ++next) {
		bands.edge[static_cast<size_t>(next)] = COUNT;
	}
	bands.edge[static_cast<size_t>(bands.count)] = COUNT;
	return finish(bands);
}

Bands bands_cut_at(std::span<const int> cuts_nm) {
	Bands bands;
	bands.count = static_cast<int>(cuts_nm.size()) + 1 > MAX_BANDS ? MAX_BANDS : static_cast<int>(cuts_nm.size()) + 1;
	bands.edge[0] = 0;
	for (int k = 1; k < bands.count; ++k) {
		bands.edge[static_cast<size_t>(k)] = cuts_nm[static_cast<size_t>(k) - 1] - FIRST_NM;
	}
	bands.edge[static_cast<size_t>(bands.count)] = COUNT;
	return finish(bands);
}

void band_averages(const Bands &bands, const Spectrum &s, std::span<double> out, bool emitted) {
	for (int k = 0; k < bands.count && static_cast<size_t>(k) < out.size(); ++k) {
		double total = 0.0, sum = 0.0;
		for (int i = bands.edge[static_cast<size_t>(k)]; i < bands.edge[static_cast<size_t>(k) + 1]; ++i) {
			const auto u = static_cast<size_t>(i);
			const double w = emitted ? xbar()[u] + ybar()[u] + zbar()[u] : weight(i);
			total += w;
			sum += w * s[u];
		}
		out[static_cast<size_t>(k)] = total > 0.0 ? sum / total : 0.0;
	}
}

Xyz colour(const Bands &bands, std::span<const double> factor) {
	Xyz out;
	for (int k = 0; k < bands.count && static_cast<size_t>(k) < factor.size(); ++k) {
		const Xyz &b = bands.lit[static_cast<size_t>(k)];
		out.x += b.x * factor[static_cast<size_t>(k)];
		out.y += b.y * factor[static_cast<size_t>(k)];
		out.z += b.z * factor[static_cast<size_t>(k)];
	}
	return out;
}

Xyz colour_emitted(const Bands &bands, std::span<const double> radiance) {
	Xyz out;
	for (int k = 0; k < bands.count && static_cast<size_t>(k) < radiance.size(); ++k) {
		// unit radiance over the band: its plain colour is the sum over its nanometres
		const Xyz &b = bands.plain[static_cast<size_t>(k)];
		out.x += b.x * radiance[static_cast<size_t>(k)];
		out.y += b.y * radiance[static_cast<size_t>(k)];
		out.z += b.z * radiance[static_cast<size_t>(k)];
	}
	return out;
}

double line_at(const Line &line, double nm) {
	const double d = (nm - line.centre_nm) / line.fwhm_nm;
	return line.peak * chem::exp(-2.772588722239781 * d * d); // 4 ln 2
}

namespace {

double lerp_table(const Spectrum &s, double nm) {
	const double x = nm - FIRST_NM;
	if (x <= 0.0) {
		return s[0];
	}
	if (x >= COUNT - 1) {
		return s[COUNT - 1];
	}
	const auto i = static_cast<size_t>(x);
	const double f = x - static_cast<double>(i);
	return s[i] * (1.0 - f) + s[i + 1] * f;
}

} // namespace

Xyz observer_at(double nm) {
	return Xyz{ lerp_table(xbar(), nm), lerp_table(ybar(), nm), lerp_table(zbar(), nm) };
}

double d65_at(double nm) {
	return lerp_table(d65(), nm);
}

double display_weight(double nm) {
	const Xyz o = observer_at(nm);
	const auto mag = [](double v) { return v < 0.0 ? -v : v; };
	// XYZ to linear sRGB (IEC 61966-2-1)
	return mag(3.2404542 * o.x - 1.5371385 * o.y - 0.4985314 * o.z) + mag(-0.9692660 * o.x + 1.8760108 * o.y + 0.0415560 * o.z) +
		   mag(0.0556434 * o.x - 0.2040259 * o.y + 1.0572252 * o.z);
}

EnergyEntry EnergyTable::at(double share) const {
	const double x = (share < 0.0 ? 0.0 : share > 1.0 ? 1.0 : share) * ENERGY_STEPS;
	auto j = static_cast<size_t>(x);
	j = j > ENERGY_STEPS - 1 ? ENERGY_STEPS - 1 : j;
	const double f = x - static_cast<double>(j);
	const EnergyEntry &a = entry[j], &b = entry[j + 1];
	return EnergyEntry{ a.nm + (b.nm - a.nm) * f, Xyz{ a.up_to.x + (b.up_to.x - a.up_to.x) * f, a.up_to.y + (b.up_to.y - a.up_to.y) * f, a.up_to.z + (b.up_to.z - a.up_to.z) * f } };
}

EnergyTable energy_table(const Spectrum &continuum, std::span<const Line> lines) {
	// the wavelengths it is integrated over: a tenth of a nanometre, and an eighth of each line's width around it
	std::vector<double> grid;
	for (int k = 0; k <= 4000; ++k) {
		grid.push_back(FIRST_NM + k * 0.1);
	}
	for (const Line &l : lines) {
		const double step = l.fwhm_nm / 8.0;
		for (double nm = l.centre_nm - 4.0 * l.fwhm_nm; nm <= l.centre_nm + 4.0 * l.fwhm_nm; nm += step) {
			if (nm > FIRST_NM && nm < FIRST_NM + COUNT - 1) {
				grid.push_back(nm);
			}
		}
	}
	std::sort(grid.begin(), grid.end());
	const double w = white();
	std::vector<double> energy{ 0.0 };
	std::vector<Xyz> colour{ Xyz{} };
	for (size_t i = 0; i + 1 < grid.size(); ++i) {
		const double nm = (grid[i] + grid[i + 1]) / 2.0, width = grid[i + 1] - grid[i];
		double radiance = lerp_table(continuum, nm);
		for (const Line &l : lines) {
			radiance += line_at(l, nm);
		}
		const Xyz o = observer_at(nm);
		const Xyz &c = colour.back();
		colour.push_back(Xyz{ c.x + o.x * radiance * width / w, c.y + o.y * radiance * width / w, c.z + o.z * radiance * width / w });
		energy.push_back(energy.back() + radiance * width * display_weight(nm));
	}
	EnergyTable t;
	t.total = colour.back();
	const double total = energy.back();
	size_t i = 1;
	for (int j = 0; j <= ENERGY_STEPS; ++j) {
		const double target = total * j / ENERGY_STEPS;
		while (i < energy.size() - 1 && energy[i] < target) {
			++i;
		}
		const double span = energy[i] - energy[i - 1];
		double f = span > 0.0 ? (target - energy[i - 1]) / span : 0.0;
		f = f < 0.0 ? 0.0 : f > 1.0 ? 1.0 : f;
		const Xyz &a = colour[i - 1], &b = colour[i];
		t.entry[static_cast<size_t>(j)] = EnergyEntry{ grid[i - 1] + (grid[i] - grid[i - 1]) * f, Xyz{ a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f } };
	}
	return t;
}

const EnergyTable &room_light() {
	static const EnergyTable table = energy_table(d65());
	return table;
}

// ---- the long-wave infrared (Stage 46)

namespace {
constexpr double C1 = 1.191042972e-16; // 2 h c^2, W m2 / sr
constexpr double C2 = 0.014387769; // h c / k, m K
constexpr double LWIR_LAST_NM = LWIR_FIRST_NM + LWIR_STEP_NM * (LWIR_COUNT - 1.0);
// the integral of t^3 / (e^t - 1) from x to infinity
double tail(double x) {
	double sum = 0.0;
	for (int n = 1; n < 100000; ++n) {
		const double e = chem::exp(-n * x), dn = n;
		const double term = e * (x * x * x / dn + 3.0 * x * x / (dn * dn) + 6.0 * x / (dn * dn * dn) + 6.0 / (dn * dn * dn * dn));
		sum += term;
		if (term < sum * 1e-17) {
			break;
		}
	}
	return sum;
}
} // namespace

double normal_reflectance(double n, double k) {
	return ((n - 1.0) * (n - 1.0) + k * k) / ((n + 1.0) * (n + 1.0) + k * k);
}

double effective_index(double n, double k) {
	const double r = std::sqrt(normal_reflectance(n, k));
	return (1.0 + r) / (1.0 - r);
}

double absorption_per_cm(double k, double nm) {
	return 4.0 * 3.14159265358979323846 * k / (nm * 1e-7);
}

double lwir_response(double nm) {
	return nm >= LWIR_FIRST_NM && nm <= LWIR_LAST_NM ? 1.0 : 0.0;
}

double planck(double nm, double kelvin) {
	if (!(kelvin > 0.0)) {
		return 0.0;
	}
	const double m = nm * 1e-9, x = C2 / (m * kelvin);
	if (x > 700.0) {
		return 0.0;
	}
	return C1 / (m * m * m * m * m) / (chem::exp(x) - 1.0) * 1e-9;
}

double band_signal(double kelvin) {
	if (!(kelvin > 0.0)) {
		return 0.0;
	}
	// with x = c2 / (wavelength T): B d(wavelength) = c1 T^4 / c2^4 x^3 / (e^x - 1) dx
	const double t4 = kelvin * kelvin * kelvin * kelvin, c24 = C2 * C2 * C2 * C2;
	const double x_long = C2 / (LWIR_LAST_NM * 1e-9 * kelvin), x_short = C2 / (LWIR_FIRST_NM * 1e-9 * kelvin);
	return C1 * t4 / c24 * (tail(x_long) - tail(x_short));
}

double brightness_kelvin(double signal, double emissivity, double reflected_kelvin) {
	const double e = emissivity < 1e-6 ? 1e-6 : emissivity > 1.0 ? 1.0 : emissivity;
	const double own = (signal - (1.0 - e) * band_signal(reflected_kelvin)) / e;
	if (!(own > 0.0)) {
		return 0.0;
	}
	// band_signal rises with the temperature: bisect on the logarithm, then Newton's steps
	double lo = 1.0, hi = 10000.0;
	if (own >= band_signal(hi)) {
		return hi;
	}
	for (int i = 0; i < 200 && hi - lo > 1e-9 * hi; ++i) {
		const double mid = std::sqrt(lo * hi);
		(band_signal(mid) < own ? lo : hi) = mid;
	}
	return (lo + hi) / 2.0;
}

EnergyTable lwir_energy_table(double room_kelvin) {
	// a nanometre at a time; the energy and the signal are the same (the response weighs it, nothing else)
	const int steps = static_cast<int>(LWIR_LAST_NM - LWIR_FIRST_NM);
	std::vector<double> energy{ 0.0 };
	for (int i = 0; i < steps; ++i) {
		const double nm = LWIR_FIRST_NM + i + 0.5;
		energy.push_back(energy.back() + planck(nm, room_kelvin) * lwir_response(nm));
	}
	EnergyTable t;
	const double total = energy.back();
	size_t i = 1;
	for (int j = 0; j <= ENERGY_STEPS; ++j) {
		const double target = total * j / ENERGY_STEPS;
		while (i < energy.size() - 1 && energy[i] < target) {
			++i;
		}
		const double span = energy[i] - energy[i - 1];
		double f = span > 0.0 ? (target - energy[i - 1]) / span : 0.0;
		f = f < 0.0 ? 0.0 : f > 1.0 ? 1.0 : f;
		const double share = static_cast<double>(j) / ENERGY_STEPS;
		t.entry[static_cast<size_t>(j)] = EnergyEntry{ LWIR_FIRST_NM + static_cast<double>(i - 1) + f, Xyz{ share, share, share } };
	}
	t.total = Xyz{ 1.0, 1.0, 1.0 };
	t.signal = total; // (each nanometre's radiance at its middle, times a nanometre)
	return t;
}

const Bands &game_bands() {
	// chosen by measurement (chem_tool study spectrum, docs/TDD_10): the fewest
	// bands within the threshold whose cuts are not fitted to the test colours
	static const Bands bands = bands_equal_weight(GAME_BANDS);
	return bands;
}

} // namespace chem::optics
