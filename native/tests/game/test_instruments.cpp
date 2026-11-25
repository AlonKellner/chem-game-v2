// The lab's instruments (GAME_SPEC §13, Readings; §18 I1; Stage 50): the seed's draws, a reading, and a balance's
// conventional mass held to OIML D 28 (docs/DATA_SOURCES.md, "The balance (Stage 50)").
#include "lab/instruments.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <string>

using namespace lab::instruments;

TEST_CASE("measured: a balance reads OIML's conventional mass (Stage 50)") {
	// OIML D 28's own case: a kilogram of water at 20 C (998.21 kg/m^3) in air of 1.2 kg/m^3, against weights of
	// 8000 kg/m^3: (1000 - 1.2e-3 * 1001.80) / (1 - 1.2 / 8000) = 998.948 g
	CHECK(reference_density() == 8000.0);
	const double v = 1000.0 / 0.99821;
	CHECK(std::fabs(conventional_g(1000.0, v, 1.2) - 998.948) < 0.001);
	// a body of the weights' own density reads its mass, in any air
	CHECK(std::fabs(conventional_g(500.0, 500.0 / 8.0, 1.2) - 500.0) < 1e-9);
	CHECK(std::fabs(conventional_g(500.0, 500.0 / 8.0, 0.9) - 500.0) < 1e-9);
	// a volume of the air itself weighs nothing on it (an open vessel's air)
	CHECK(std::fabs(conventional_g(1.2e-3 * 250.0, 250.0, 1.2)) < 1e-12);
}

TEST_CASE("instruments: a seed draws each instrument's offset once, spread over its error (Stage 50)") {
	CHECK(share(7, "beaker:thermometer") == share(7, "beaker:thermometer"));
	CHECK(share(7, "beaker:thermometer") != share(8, "beaker:thermometer"));
	CHECK(share(7, "beaker:thermometer") != share(7, "beaker:ph"));
	// uniform over [-1, 1]: the mean near 0, a quarter in each quarter
	double sum = 0.0;
	int quarters[4] = {};
	const int n = 20000;
	for (int i = 0; i < n; ++i) {
		const double u = share(static_cast<uint64_t>(i % 97), "vessel" + std::to_string(i));
		REQUIRE(u >= -1.0);
		REQUIRE(u < 1.0);
		sum += u;
		++quarters[std::min(3, static_cast<int>((u + 1.0) * 2.0))];
	}
	CHECK(std::fabs(sum / n) < 0.02);
	for (const int q : quarters) {
		CHECK(std::fabs(q / static_cast<double>(n) - 0.25) < 0.015);
	}
	// the data's instruments
	REQUIRE(balances().size() == 2);
	CHECK(balance("top_loading")->readability_g == 0.01);
	CHECK(balance("analytical")->readability_g == 0.0001);
	CHECK(balance("none") == nullptr);
	CHECK(thermometer().resolution == 0.1);
	CHECK(ph_meter().accuracy == 0.02);
	CHECK(hand() == 0.10);
}

TEST_CASE("instruments: I1 holds of readings and fires on one past its error or off its resolution (Stage 50)") {
	const Meter t = thermometer();
	std::vector<Read> reads;
	for (int i = 0; i < 2000; ++i) {
		const double truth = 15.0 + 0.0137 * i;
		const double offset = t.accuracy * share(3, "v" + std::to_string(i));
		reads.push_back(Read{ "thermometer", truth, rounded(truth + offset, t.resolution), t.accuracy, t.resolution });
		const Balance &b = *balance("analytical");
		const double gross = 3.0 + 0.091 * i, tare = 0.5 * gross;
		reads.push_back(Read{ "balance", gross - tare, display_g(b, gross, tare, share(3, "b" + std::to_string(i))), b.linearity_g, b.readability_g });
		const double ml = 1.0 + 0.123 * i;
		reads.push_back(Read{ "marks", ml, marks_ml(ml, 25.0, 0.05, share(3, "m" + std::to_string(i))), 0.05 * ml, 25.0 });
	}
	CHECK(check_I1(reads) == "");
	// past its error: an offset drawn at 1.5 of the accuracy
	std::vector<Read> past{ Read{ "thermometer", 20.0, rounded(20.0 + 1.5 * t.accuracy + t.resolution, t.resolution), t.accuracy, t.resolution } };
	CHECK(check_I1(past).rfind("I1", 0) == 0);
	// rounded to the wrong resolution: a hundredth where it reads tenths
	std::vector<Read> fine{ Read{ "thermometer", 20.0, 20.03, t.accuracy, t.resolution } };
	CHECK(check_I1(fine).rfind("I1", 0) == 0);
	// a balance with no linearity's bound: its whole linearity on the net, twice
	const Balance &b = *balance("top_loading");
	std::vector<Read> lin{ Read{ "balance", 300.0, rounded(300.0 + 2.0 * b.linearity_g + b.readability_g, b.readability_g), b.linearity_g, b.readability_g } };
	CHECK(check_I1(lin).rfind("I1", 0) == 0);
}

TEST_CASE("instruments: a balance's display is its net to its readability, its linearity in proportion to the load (Stage 50)") {
	const Balance &b = *balance("top_loading");
	CHECK(display_g(b, 0.0, 0.0, 0.7) == 0.0); // an empty pan reads 0
	CHECK(std::fabs(display_g(b, 123.456, 123.456, -0.9)) < 1e-12); // tared, it reads 0
	CHECK(std::fabs(display_g(b, 123.456, 0.0, 0.0) - 123.46) < 1e-9);
	// the whole linearity at its capacity, none at no load
	CHECK(std::fabs(linearity_g(b, b.capacity_g, 1.0) - b.linearity_g) < 1e-12);
	CHECK(linearity_g(b, 0.0, 1.0) == 0.0);
	CHECK(std::fabs(linearity_g(b, 2.0 * b.capacity_g, -1.0) + b.linearity_g) < 1e-12);
	// marks: to the nearest one
	CHECK(marks_ml(118.0, 25.0, 0.05, 0.0) == 125.0);
	CHECK(marks_ml(112.0, 25.0, 0.05, 0.0) == 100.0);
	CHECK(marks_ml(100.0, 10.0, 0.05, 1.0) == 110.0); // 105 to the nearest 10 (half rounds away)
}

TEST_CASE("measured: a thermometer reaches 90 % of a step in its probe's 10 s (Stage 52)") {
	// the Vernier TMP-BTA's t90 in stirred water, as the lab steps it (1/60 s)
	const double tau = thermometer().time_constant_s;
	double reading = 20.0, seconds = 0.0;
	while (reading < 20.0 + 0.9 * 30.0 && seconds < 60.0) {
		reading = lagged(reading, 50.0, 1.0 / 60.0, tau);
		seconds += 1.0 / 60.0;
	}
	CHECK(seconds == doctest::Approx(10.0).epsilon(0.01));
	CHECK(lagged(20.0, 50.0, 1.0, 0.0) == 50.0); // no lag: at once
}
