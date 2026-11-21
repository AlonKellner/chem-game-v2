// Energy bands with moving points (GAME_SPEC §12.2): each light's energy table,
// pure water, the eye metric and gamut mapping, and O3, the random spectra.
#include "random_spectra.hpp"

#include "lab/colour.hpp"

#include "chem/builtin_ids.hpp"
#include "chem/optics.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using chem::optics::EnergyTable;
using chem::optics::Line;
using chem::optics::Xyz;

namespace {

// O3's bounds (GAME_SPEC §18): measured, then set a little above (docs/TDD_10_OPTICS.md)
constexpr double O3_SETTLED_MEAN = 0.25; // measured 0.11
constexpr double O3_FIRST_VISIBLE = 0.005; // measured 1 in 1,500

double frac(double v) {
	return v - std::floor(v);
}

// The colour of the room's light through `through`, integrated finely enough for any line in it: a
// tenth of a nanometre, and an eighth of each line's width around it. On the room's scale: nothing in
// the way gives the room light's own colour.
template <class Through>
Xyz truth(Through through, const std::vector<Line> &lines = {}) {
	std::vector<double> grid;
	for (int k = 0; k <= 4000; ++k) {
		grid.push_back(380.0 + k * 0.1);
	}
	for (const Line &l : lines) {
		for (double nm = l.centre_nm - 4.0 * l.fwhm_nm; nm <= l.centre_nm + 4.0 * l.fwhm_nm; nm += l.fwhm_nm / 8.0) {
			if (nm > 380.0 && nm < 780.0) {
				grid.push_back(nm);
			}
		}
	}
	std::sort(grid.begin(), grid.end());
	Xyz sum;
	double white = 0.0;
	for (size_t i = 0; i + 1 < grid.size(); ++i) {
		const double nm = (grid[i] + grid[i + 1]) / 2.0, w = (grid[i + 1] - grid[i]) * chem::optics::d65_at(nm);
		const Xyz o = chem::optics::observer_at(nm);
		const double t = through(nm);
		sum.x += o.x * w * t;
		sum.y += o.y * w * t;
		sum.z += o.z * w * t;
		white += o.y * w;
	}
	const double scale = chem::optics::room_light().total.y / white;
	return Xyz{ sum.x * scale, sum.y * scale, sum.z * scale };
}

// the game's estimate settled: n points a frame, the offset moving by the golden ratio, `frames` averaged
template <class Through>
Xyz settled(int n, int frames, double seed, Through through) {
	Xyz sum;
	for (int f = 0; f < frames; ++f) {
		const Xyz e = chem::optics::estimate(chem::optics::room_light(), n, frac(seed + 0.6180339887 * f), through);
		sum.x += e.x / frames;
		sum.y += e.y / frames;
		sum.z += e.z / frames;
	}
	return sum;
}

double eye(const Xyz &a, const Xyz &b) {
	return lab::eye_difference(lab::shown(a), lab::shown(b));
}

// O3 over a deterministic set of random spectra (GAME_SPEC §12.2, §18)
struct O3Result {
	int cases = 0;
	int settled_visible = 0; // settled further than the eye's threshold from the truth
	double settled_worst = 0.0, settled_mean = 0.0;
	int first_visible = 0; // the first frame after a change (FRESH points)
	double first_mean = 0.0;
	int worst_family = -1;
	double family_worst[spectra::FAMILIES] = {};
};

O3Result o3(int batch, int frames, int fresh, int cases, uint64_t seed) {
	spectra::Random random(seed);
	O3Result r;
	for (int c = 0; c < cases; ++c) {
		// what is in the way: an absorber at a depth, sometimes over a reflector
		const spectra::Absorber a = random.absorber();
		double depth = random.log_uniform(0.01, 50.0);
		if (a.family == 9) {
			depth = std::min(depth, 0.5); // an interference ripple is shallow
		}
		const bool reflect = random.pick(3) == 0;
		const spectra::Absorber r0 = random.absorber();
		const double lo = random.uniform(0, 0.6), hi = random.uniform(lo, 1.0);
		const double light = random.pick(2) ? 0.7305 : 1.0;
		const auto through = [&](double nm) {
			double t = light * std::exp(-depth * a.at(nm));
			if (reflect) {
				t *= lo + (hi - lo) * std::min(1.0, r0.at(nm));
			}
			return t;
		};
		std::vector<Line> lines = a.lines;
		if (reflect) {
			lines.insert(lines.end(), r0.lines.begin(), r0.lines.end());
		}
		const Xyz t = truth(through, lines);
		const double s = random.uniform(0, 1);
		const double e = eye(t, settled(batch, frames, s, through));
		const double first = eye(t, chem::optics::estimate(chem::optics::room_light(), fresh, s, through));
		++r.cases;
		r.settled_visible += e > lab::EYE_VISIBLE ? 1 : 0;
		r.settled_mean += e / cases;
		r.first_visible += first > lab::EYE_VISIBLE ? 1 : 0;
		r.first_mean += first / cases;
		r.family_worst[a.family] = std::max(r.family_worst[a.family], e);
		if (e > r.settled_worst) {
			r.settled_worst = e;
			r.worst_family = a.family;
		}
	}
	return r;
}

} // namespace

TEST_CASE("optics: a light's energy table holds its whole colour, cut in equal shares of what the display makes of it (Stage 10)") {
	const EnergyTable &room = chem::optics::room_light();
	chem::optics::Spectrum one;
	one.fill(1.0);
	const Xyz white = chem::optics::colour(one);
	CHECK(room.total.x == doctest::Approx(white.x).epsilon(1e-3));
	CHECK(room.total.y == doctest::Approx(white.y).epsilon(1e-3));
	CHECK(room.total.z == doctest::Approx(white.z).epsilon(1e-3));
	CHECK(room.entry.front().nm == doctest::Approx(380.0));
	CHECK(room.entry.back().nm == doctest::Approx(780.0));
	for (int j = 0; j < chem::optics::ENERGY_STEPS; ++j) {
		REQUIRE(room.entry[static_cast<size_t>(j)].nm <= room.entry[static_cast<size_t>(j) + 1].nm);
		REQUIRE(room.entry[static_cast<size_t>(j)].up_to.y <= room.entry[static_cast<size_t>(j) + 1].up_to.y + 1e-15);
	}
	// sixteen bands each hold a sixteenth of the light's display-weighted energy
	double energy[16] = {}, total = 0.0;
	for (double nm = 380.005; nm < 780.0; nm += 0.01) {
		const double e = chem::optics::d65_at(nm) * chem::optics::display_weight(nm);
		total += e;
		for (int k = 0; k < 16; ++k) {
			if (nm >= room.at(k / 16.0).nm && nm < room.at((k + 1) / 16.0).nm) {
				energy[k] += e;
			}
		}
	}
	for (const double e : energy) {
		CHECK(e / total == doctest::Approx(1.0 / 16.0).epsilon(0.01));
	}
}

TEST_CASE("optics: a lamp of narrow lines: each line owns its share of the bands, and no frame loses any of its light (Stage 10)") {
	// a sodium lamp: the D lines, a tenth of a nanometre wide, the first twice the second
	chem::optics::Spectrum dark{};
	const std::vector<Line> d_lines{ { 588.995, 0.1, 2.0 }, { 589.592, 0.1, 1.0 } };
	const EnergyTable lamp = chem::optics::energy_table(dark, d_lines);
	int first = 0, second = 0, elsewhere = 0;
	for (const auto &e : lamp.entry) {
		(std::fabs(e.nm - 588.995) < 0.3 ? first : std::fabs(e.nm - 589.592) < 0.3 ? second : elsewhere)++;
	}
	CHECK(elsewhere <= 2);
	CHECK(static_cast<double>(first) / (first + second) == doctest::Approx(2.0 / 3.0).epsilon(0.02));
	// nothing in the way: every frame, any number of points, any offset, carries the whole light
	for (const int n : { 1, 2, 3, 16, 64 }) {
		for (const double offset : { 0.0, 0.31, 0.999 }) {
			const Xyz e = chem::optics::estimate(lamp, n, offset, [](double) { return 1.0; });
			CHECK(e.y == doctest::Approx(lamp.total.y).epsilon(1e-12));
			CHECK(e.x == doctest::Approx(lamp.total.x).epsilon(1e-12));
		}
	}
	// a filter that stops one line: the points find it, however few
	const auto stop_second = [](double nm) { return nm > 589.3 ? 0.0 : 1.0; };
	Xyz mean;
	for (int f = 0; f < 64; ++f) {
		const Xyz e = chem::optics::estimate(lamp, 4, frac(0.2 + 0.6180339887 * f), stop_second);
		mean.y += e.y / 64;
	}
	CHECK(mean.y == doctest::Approx(lamp.total.y * 2.0 / 3.0).epsilon(0.01));
}

TEST_CASE("optics: a flat factor is exact in every frame; the moving points settle to the exact colour (Stage 10)") {
	const EnergyTable &room = chem::optics::room_light();
	const Xyz half = chem::optics::estimate(room, 16, 0.37, [](double) { return 0.5; });
	CHECK(half.y == doctest::Approx(room.total.y * 0.5).epsilon(1e-12));
	const chem::optics::Spectrum &salt = chem::optics::spectrum(chem::builtin::copper_sulfate_aq);
	for (const double absorbance : { 0.1, 1.0, 4.0 }) {
		const auto through = [&](double nm) {
			const double x = nm - 380.0;
			const auto i = static_cast<size_t>(std::min(399.0, std::max(0.0, std::floor(x))));
			const double f = x - static_cast<double>(i);
			return std::pow(10.0, -absorbance * (salt[i] * (1.0 - f) + salt[i + 1] * f));
		};
		const Xyz t = truth(through);
		CHECK(eye(t, settled(16, 64, 0.1, through)) < 1.5);
		CHECK(eye(t, chem::optics::exact(room, through)) < 0.5);
	}
}

TEST_CASE("optics: pure water, as measured: a beaker of it is clear, a column of metres is blue (Stage 10)") {
	// the table's own value back: 550 nm, (0.0565 + 0.0014) per metre, over water's 55.345 mol/L
	const chem::optics::Spectrum &water = chem::optics::spectrum(chem::builtin::water);
	CHECK(chem::optics::kind(chem::builtin::water) == chem::optics::Kind::Absorb);
	const double per_metre = water[170] * 2.302585092994046 * 55.345 * 100.0;
	CHECK(per_metre == doctest::Approx(0.0579).epsilon(1e-3));
	CHECK(water[320] * 2.302585092994046 * 55.345 * 100.0 == doctest::Approx(0.624 + 0.0005).epsilon(1e-3)); // 700 nm
	// 755-780 nm: Kou, Labrie and Chylek (1993), with Morel's scattering: 760 nm 2.8605 + 0.000378, 780 nm 2.6905 + 0.000338 per metre
	CHECK(water[380] * 2.302585092994046 * 55.345 * 100.0 == doctest::Approx(2.8605 + 0.000378).epsilon(1e-3));
	CHECK(water[400] * 2.302585092994046 * 55.345 * 100.0 == doctest::Approx(2.6905 + 0.000338).epsilon(1e-3));
	const auto column = [&](double metres) {
		return truth([&](double nm) {
			const double x = nm - 380.0;
			const auto i = static_cast<size_t>(std::min(399.0, std::max(0.0, std::floor(x))));
			const double f = x - static_cast<double>(i);
			return std::exp(-(water[i] * (1.0 - f) + water[i + 1] * f) * 2.302585092994046 * 55.345 * 100.0 * metres);
		});
	};
	const Xyz white = chem::optics::room_light().total;
	CHECK(eye(white, column(0.1)) < 2.0); // a beaker: clear
	int code[3];
	const lab::CieLab deep = lab::shown(column(3.0), code);
	CHECK(deep.b < -5.0); // three metres: blue
	CHECK(code[2] > code[0] + 20);
	CHECK(code[1] > code[0]); // greener than red: the blue-green of a pool
}

TEST_CASE("colour: the eye metric weighs lightness most and a grey shown as a colour (Stage 10)") {
	const lab::CieLab grey{ 50, 0, 0 };
	CHECK(lab::eye_difference(grey, grey) == 0.0);
	// two units of lightness count more than two of chroma
	CHECK(lab::eye_difference(grey, { 52, 0, 0 }) > lab::eye_difference(grey, { 50, 2, 0 }));
	// a grey gaining chroma counts more than a colour gaining the same
	CHECK(lab::eye_difference(grey, { 50, 4, 0 }) > lab::eye_difference({ 50, 30, 0 }, { 50, 34, 0 }));
	CHECK(lab::eye_difference({ 50, 4, 0 }, grey) == doctest::Approx(lab::eye_difference(grey, { 50, 4, 0 })).epsilon(0.05));
}

TEST_CASE("colour: a colour no display has keeps its lightness and hue (Stage 10)") {
	// in gamut: unchanged
	double inside[3] = { 0.2, 0.5, 0.7 };
	lab::gamut_map(inside);
	CHECK(inside[0] == doctest::Approx(0.2));
	CHECK(inside[2] == doctest::Approx(0.7));
	// a saturated cyan beyond sRGB: in range, hue kept, lightness kept within a little
	double cyan[3] = { -0.15, 0.6, 0.65 };
	const double y_before = 0.2126 * cyan[0] + 0.7152 * cyan[1] + 0.0722 * cyan[2];
	lab::gamut_map(cyan);
	for (const double c : cyan) {
		CHECK(c >= 0.0);
		CHECK(c <= 1.0);
	}
	CHECK(cyan[0] < cyan[1]);
	CHECK(cyan[0] < cyan[2]);
	const double y_after = 0.2126 * cyan[0] + 0.7152 * cyan[1] + 0.0722 * cyan[2];
	CHECK(y_after == doctest::Approx(y_before).epsilon(0.08));
	// brighter than white is white
	double bright[3] = { 1.5, 1.4, 1.6 };
	lab::gamut_map(bright);
	CHECK(bright[1] == 1.0);
}

TEST_CASE("O3: random spectra of every shape are shown within the eye's threshold, settled and right after a change (Stage 10)") {
	const O3Result r = o3(16, 64, 64, 1500, 2026);
	std::string families;
	for (int f = 0; f < spectra::FAMILIES; ++f) {
		char text[64];
		std::snprintf(text, sizeof text, " %s %.2f", spectra::FAMILY[f], r.family_worst[f]);
		families += text;
	}
	MESSAGE("O3, " << r.cases << " random spectra: settled worst " << r.settled_worst << " (" << std::string(spectra::FAMILY[r.worst_family < 0 ? 0 : r.worst_family]) << "), mean " << r.settled_mean
				   << ", visible " << r.settled_visible << "; the first frame after a change (64 points): mean " << r.first_mean << ", visible " << r.first_visible
				   << "; worst by family:" << families);
	CHECK(r.settled_visible == 0);
	CHECK(r.settled_mean <= O3_SETTLED_MEAN);
	CHECK(static_cast<double>(r.first_visible) / r.cases <= O3_FIRST_VISIBLE);
}

TEST_CASE("O3: the check fires on batches too small to settle on the right colour (Stage 10)") {
	const O3Result r = o3(2, 64, 64, 400, 2026);
	CHECK(r.settled_visible > 0);
}
