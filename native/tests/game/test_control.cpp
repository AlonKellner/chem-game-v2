// A controller's tuning (GAME_SPEC §13.3, Stage 26): the fit of a step's response, and SIMC's gains held to
// their source's own figures (Skogestad 2003; docs/DATA_SOURCES.md, "SIMC").
#include "lab/control.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using lab::Margins;
using lab::PiGains;
using lab::ProcessModel;

TEST_CASE("control: SIMC's tight tuning (tau_c = theta) has its source's margins: a lag no longer than 8 theta, gain margin pi and phase margin 61.4 degrees; an integrator, 2.96 and 46.9 (Stage 26)") {
	// a first-order process with a delay, its lag short enough that Ti is the lag: the loop is e^-theta s / (2 theta s)
	for (const ProcessModel m : { ProcessModel{ 2.0, 5.0, 1.0, false }, ProcessModel{ -0.3, 40.0, 6.0, false }, ProcessModel{ 7.0, 0.5, 0.5, false } }) {
		const PiGains g = lab::simc(m, m.theta, 0.0);
		CHECK(g.ti == doctest::Approx(m.tau));
		CHECK(g.kc * m.gain > 0.0); // its direction is the process's
		const Margins mg = lab::margins(m, g);
		CHECK(mg.gain == doctest::Approx(M_PI).epsilon(1e-6));
		CHECK(mg.phase == doctest::Approx(61.35).epsilon(2e-3));
	}
	// an integrating process: Kc = 1 / (k' 2 theta), Ti = 8 theta
	for (const ProcessModel m : { ProcessModel{ 0.5, 0.0, 2.0, true }, ProcessModel{ 3.0, 0.0, 0.25, true } }) {
		const PiGains g = lab::simc(m, m.theta, 0.0);
		CHECK(g.kc == doctest::Approx(1.0 / (m.gain * 2.0 * m.theta)));
		CHECK(g.ti == doctest::Approx(8.0 * m.theta));
		const Margins mg = lab::margins(m, g);
		CHECK(mg.gain == doctest::Approx(2.96).epsilon(5e-3));
		CHECK(mg.phase == doctest::Approx(46.9).epsilon(5e-3));
	}
	// a slower closed loop is more robust
	const ProcessModel m{ 1.0, 0.0, 1.0, true };
	CHECK(lab::margins(m, lab::simc(m, 4.0, 0.0)).phase > lab::margins(m, lab::simc(m, 1.0, 0.0)).phase);
	// the controller's own delay is the least it takes
	CHECK(lab::simc(ProcessModel{ 1.0, 0.0, 0.0, true }, 0.0, 1.0 / 30.0).kc == doctest::Approx(30.0));
}

TEST_CASE("control: a step's response is fitted: a first-order process with a delay, and an integrating one (Stage 26)") {
	std::vector<double> t, first, rising;
	for (int i = 1; i <= 200; ++i) {
		const double s = 0.5 * i;
		t.push_back(s);
		first.push_back(s > 3.0 ? 12.0 * (1.0 - std::exp(-(s - 3.0) / 20.0)) : 0.0);
		rising.push_back(s > 2.0 ? -0.8 * (s - 2.0) : 0.0);
	}
	ProcessModel m;
	REQUIRE(lab::fit_process(t, first, m));
	CHECK_FALSE(m.integrating);
	CHECK(m.gain == doctest::Approx(12.0).epsilon(0.03));
	CHECK(m.tau == doctest::Approx(20.0).epsilon(0.1));
	CHECK(m.theta == doctest::Approx(3.0).epsilon(0.2));
	REQUIRE(lab::fit_process(t, rising, m));
	CHECK(m.integrating);
	CHECK(m.gain == doctest::Approx(-0.8).epsilon(0.01));
	CHECK(m.theta == doctest::Approx(2.0).epsilon(0.1));
	// nothing moved: nothing fitted
	CHECK_FALSE(lab::fit_process(t, std::vector<double>(t.size(), 0.0), m));
	// the speeds: gentle the process's own time, normal a quarter, fast a twentieth, never under the delay
	const ProcessModel slow{ 1.0, 100.0, 2.0, false };
	CHECK(lab::closed_loop_time(slow, lab::Speed::Gentle, 100.0) == 100.0);
	CHECK(lab::closed_loop_time(slow, lab::Speed::Normal, 100.0) == 25.0);
	CHECK(lab::closed_loop_time(slow, lab::Speed::Fast, 100.0) == 5.0);
	CHECK(lab::closed_loop_time(ProcessModel{ 1.0, 10.0, 2.0, false }, lab::Speed::Fast, 10.0) == 2.0);
}
