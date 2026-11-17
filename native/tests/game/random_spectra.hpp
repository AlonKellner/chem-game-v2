#pragma once
// Random spectra of every realistic shape (GAME_SPEC §12.2, the test set): the
// eleven families of the user's blind rounds, and narrow lines kept as lines.
#include "chem/optics.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace spectra {

using chem::optics::Line;
using chem::optics::Spectrum;

inline constexpr int FAMILIES = 12;
inline const char *const FAMILY[FAMILIES] = { "gauss", "gauss-wavenumber", "lorentz", "skewed", "narrow", "edge", "power", "progression", "noise", "ripple", "flat", "lines" };

// what an absorber is: its 1 nm continuum, normalised to a peak of 1, and lines beside it
struct Absorber {
	Spectrum continuum{};
	std::vector<Line> lines;
	int family = 0;
	double at(double nm) const {
		const double x = nm - chem::optics::FIRST_NM;
		double v;
		if (x <= 0.0) {
			v = continuum[0];
		} else if (x >= chem::optics::COUNT - 1) {
			v = continuum[chem::optics::COUNT - 1];
		} else {
			const auto i = static_cast<size_t>(x);
			const double f = x - static_cast<double>(i);
			v = continuum[i] * (1.0 - f) + continuum[i + 1] * f;
		}
		for (const Line &l : lines) {
			v += chem::optics::line_at(l, nm);
		}
		return v;
	}
};

class Random {
public:
	explicit Random(uint64_t seed) :
			rng_(seed) {}
	double uniform(double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng_); }
	double log_uniform(double a, double b) { return std::exp(uniform(std::log(a), std::log(b))); }
	int pick(int n) { return static_cast<int>(rng_() % static_cast<uint64_t>(n)); }

	// one shape, peak 1
	Absorber shape(int family) {
		Absorber a;
		a.family = family;
		Spectrum &s = a.continuum;
		const double ln2x4 = 4.0 * std::log(2.0), c = uniform(360, 800);
		const auto nm = [](size_t i) { return 380.0 + static_cast<double>(i); };
		switch (family) {
			case 0: {
				const double w = log_uniform(5, 300);
				for (size_t i = 0; i < 401; ++i) {
					const double d = (nm(i) - c) / w;
					s[i] = std::exp(-ln2x4 * d * d);
				}
				break;
			}
			case 1: {
				const double nu0 = 1e7 / c, w = log_uniform(300, 9000);
				for (size_t i = 0; i < 401; ++i) {
					const double d = (1e7 / nm(i) - nu0) / w;
					s[i] = std::exp(-ln2x4 * d * d);
				}
				break;
			}
			case 2: {
				const double w = log_uniform(1, 200);
				for (size_t i = 0; i < 401; ++i) {
					const double d = 2 * (nm(i) - c) / w;
					s[i] = 1 / (1 + d * d);
				}
				break;
			}
			case 3: {
				const double wl = log_uniform(3, 200), wr = wl * log_uniform(0.1, 10), tail = pick(2) ? log_uniform(2, 150) : 0.0;
				for (size_t i = 0; i < 401; ++i) {
					const double x = nm(i) - c, d = x / (x < 0 ? wl : wr);
					s[i] = std::exp(-ln2x4 * d * d);
					if (tail > 0 && x > 0) {
						s[i] = std::max(s[i], std::exp(-x / tail));
					}
				}
				if (pick(2)) {
					std::reverse(s.begin(), s.end());
				}
				break;
			}
			case 4: {
				const double w = log_uniform(0.3, 4);
				const bool lorentz = pick(2);
				for (size_t i = 0; i < 401; ++i) {
					const double d = (nm(i) - c) / w;
					s[i] = lorentz ? 1 / (1 + 4 * d * d) : std::exp(-ln2x4 * d * d);
				}
				break;
			}
			case 5: {
				const double edge = uniform(370, 790), w = log_uniform(0.5, 60);
				const bool up = pick(2);
				for (size_t i = 0; i < 401; ++i) {
					const double z = (nm(i) - edge) / w;
					s[i] = 1 / (1 + std::exp(up ? -z : z));
				}
				break;
			}
			case 6: {
				const double p = uniform(0.5, 6);
				const bool up = pick(2);
				for (size_t i = 0; i < 401; ++i) {
					s[i] = std::pow(nm(i) / 550.0, up ? p : -p);
				}
				break;
			}
			case 7: {
				const double spacing = uniform(8, 40), w = uniform(2, spacing), huang = uniform(0.5, 5);
				double f = 1;
				for (int n = 0; n < 12; ++n) {
					if (n) {
						f *= huang / n;
					}
					for (size_t i = 0; i < 401; ++i) {
						const double z = (nm(i) - (c - n * spacing)) / w;
						s[i] += f * std::exp(-ln2x4 * z * z);
					}
				}
				break;
			}
			case 8: {
				const double ell = log_uniform(1, 150);
				std::vector<double> g(801);
				std::normal_distribution<double> normal;
				for (double &v : g) {
					v = normal(rng_);
				}
				const int r = std::min(static_cast<int>(std::ceil(3 * ell)), 200);
				for (size_t i = 0; i < 401; ++i) {
					double t = 0, ws = 0;
					for (int d = -r; d <= r; ++d) {
						const double w = std::exp(-0.5 * d * d / (ell * ell));
						t += w * g[i + 200 + static_cast<size_t>(d + 200) - 200];
						ws += w * w;
					}
					s[i] = t / std::sqrt(ws);
				}
				const double amplitude = uniform(0.3, 2.5);
				for (double &v : s) {
					v = std::exp(amplitude * v);
				}
				break;
			}
			case 9: {
				const double path = log_uniform(500, 30000), phase = uniform(0, 6.3);
				for (size_t i = 0; i < 401; ++i) {
					s[i] = 0.5 + 0.5 * std::cos(2 * 3.14159265358979 * path / nm(i) + phase);
				}
				break;
			}
			case 10: {
				const double slope = uniform(-0.3, 0.3);
				for (size_t i = 0; i < 401; ++i) {
					s[i] = 1 + slope * (static_cast<double>(i) - 200.0) / 200.0;
				}
				break;
			}
			default: { // many narrow lines, a twentieth of a nanometre to two nanometres wide, kept as lines
				const int count = 3 + pick(38);
				for (int k = 0; k < count; ++k) {
					a.lines.push_back(Line{ uniform(385, 775), log_uniform(0.05, 2.0), log_uniform(0.2, 1.0) });
				}
				return a;
			}
		}
		const double most = *std::max_element(s.begin(), s.end());
		for (double &v : s) {
			v = most > 0 ? v / most : 0;
		}
		return a;
	}

	// a spectrum: one shape, or a mixture of two or three
	Absorber absorber(int force = -1) {
		const int parts = force >= 0 ? 1 : (pick(3) == 0 ? 2 + pick(2) : 1);
		Absorber out;
		for (int p = 0; p < parts; ++p) {
			const Absorber one = shape(force >= 0 ? force : pick(FAMILIES));
			const double h = p ? uniform(0.05, 1) : 1;
			for (size_t i = 0; i < out.continuum.size(); ++i) {
				out.continuum[i] += h * one.continuum[i];
			}
			for (Line l : one.lines) {
				l.peak *= h;
				out.lines.push_back(l);
			}
			out.family = one.family;
		}
		return out;
	}

private:
	std::mt19937_64 rng_;
};

} // namespace spectra
