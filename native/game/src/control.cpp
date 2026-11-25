// A controller's tuning (GAME_SPEC §13.3, Stage 26): a step's response fitted, SIMC's PI gains.
#include "lab/control.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace lab {

namespace {

// the least squares gain of a shape to the response, and what is left
void least_squares(const std::vector<double> &r, const std::vector<double> &phi, double &gain, double &left) {
	double rp = 0.0, pp = 0.0;
	for (size_t i = 0; i < r.size(); ++i) {
		rp += r[i] * phi[i];
		pp += phi[i] * phi[i];
	}
	gain = pp > 0.0 ? rp / pp : 0.0;
	left = 0.0;
	for (size_t i = 0; i < r.size(); ++i) {
		const double e = r[i] - gain * phi[i];
		left += e * e;
	}
}

std::complex<double> loop_at(const ProcessModel &m, const PiGains &g, double w) {
	const std::complex<double> s(0.0, w);
	const std::complex<double> c = g.kc * (1.0 + 1.0 / (g.ti * s));
	const std::complex<double> delay = std::exp(-s * m.theta);
	const std::complex<double> p = m.integrating ? m.gain * delay / s : m.gain * delay / (m.tau * s + 1.0);
	return c * p;
}

// the loop's phase, unwrapped: the controller's, the delay's and the lag's, each continuous in w (a tuned loop's
// gains are of one sign, the process's and the controller's: their signs cancel)
double phase_at(const ProcessModel &m, const PiGains &g, double w) {
	const double controller = std::atan2(-1.0 / (g.ti * w), 1.0);
	const double process = -w * m.theta - (m.integrating ? M_PI / 2.0 : std::atan(w * m.tau));
	return controller + process;
}

} // namespace

bool fit_process(const std::vector<double> &t, const std::vector<double> &r, ProcessModel &out) {
	const size_t n = std::min(t.size(), r.size());
	if (n < 3) {
		return false;
	}
	double largest = 0.0;
	for (size_t i = 0; i < n; ++i) {
		largest = std::max(largest, std::fabs(r[i]));
	}
	if (!(largest > LEAST_RESPONSE)) {
		return false; // (Stage 50: a response no sensor resolves is none, as the room's air carries a trace of anything)
	}
	const double end = t[n - 1], first = t[0];
	std::vector<double> rr(r.begin(), r.begin() + static_cast<std::ptrdiff_t>(n)), phi(n);
	double best = 1e300;
	ProcessModel found;
	// delays: none, then each sampled time up to half the trial
	std::vector<double> delays{ 0.0 };
	for (size_t i = 0; i < n && t[i] <= 0.5 * end; ++i) {
		delays.push_back(t[i]);
	}
	for (const double theta : delays) {
		// first-order: time constants spaced by ratio from a tenth of the first sample to a hundred trials
		for (double tau = 0.1 * first; tau <= 100.0 * end; tau *= 1.15) {
			for (size_t i = 0; i < n; ++i) {
				phi[i] = t[i] > theta ? 1.0 - std::exp(-(t[i] - theta) / tau) : 0.0;
			}
			double gain = 0.0, left = 0.0;
			least_squares(rr, phi, gain, left);
			if (left < best) {
				best = left;
				found = ProcessModel{ gain, tau, theta, false };
			}
		}
		// integrating
		for (size_t i = 0; i < n; ++i) {
			phi[i] = std::max(0.0, t[i] - theta);
		}
		double slope = 0.0, left = 0.0;
		least_squares(rr, phi, slope, left);
		if (left <= best) {
			best = left;
			found = ProcessModel{ slope, 0.0, theta, true };
		}
	}
	// a time constant far past the trial is a process the trial saw only rising: an integrating one
	if (!found.integrating && found.tau > 10.0 * end) {
		found = ProcessModel{ found.gain / found.tau, 0.0, found.theta, true };
	}
	if (!(std::fabs(found.gain) > 0.0) || !std::isfinite(found.gain)) {
		return false;
	}
	out = found;
	return true;
}

double closed_loop_time(const ProcessModel &m, Speed speed, double span) {
	const double share = speed == Speed::Gentle ? 1.0 : speed == Speed::Normal ? 0.25 : 0.05;
	return std::max(m.theta, share * span);
}

PiGains simc(const ProcessModel &m, double tau_c, double least_delay) {
	const double theta = std::max(m.theta, least_delay);
	PiGains g;
	if (m.integrating) {
		g.kc = 1.0 / (m.gain * (tau_c + theta));
		g.ti = 4.0 * (tau_c + theta);
	} else {
		g.kc = m.tau / (m.gain * (tau_c + theta));
		g.ti = std::min(m.tau, 4.0 * (tau_c + theta));
	}
	return g;
}

Margins margins(const ProcessModel &m, const PiGains &g) {
	Margins out;
	// the crossings, by halving on a log scale: |L| falls through 1, the phase through -180 degrees
	const auto halve = [&](auto above) {
		double lo = 1e-9, hi = 1e6;
		for (int k = 0; k < 200; ++k) {
			const double mid = std::sqrt(lo * hi);
			(above(mid) ? lo : hi) = mid;
		}
		return std::sqrt(lo * hi);
	};
	const double wc = halve([&](double w) { return std::abs(loop_at(m, g, w)) > 1.0; });
	const double w180 = halve([&](double w) { return phase_at(m, g, w) > -M_PI; });
	out.phase = 180.0 + phase_at(m, g, wc) * 180.0 / M_PI;
	out.gain = 1.0 / std::abs(loop_at(m, g, w180));
	return out;
}

} // namespace lab
