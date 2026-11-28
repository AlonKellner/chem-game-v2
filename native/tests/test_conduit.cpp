// A conduit's flow law (GAME_SPEC §6, Flow between vessels, Stage 89): round pieces in series, the drop a mass rate
// takes through them from their geometry alone (Churchill's friction, Shah's developing flow, a contraction's and an
// expansion's losses), one function of the rate for a liquid and a gas, and a gas's choked limit.
#include "chem/conduit.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

using namespace chem;

namespace {

constexpr double PI = 3.14159265358979323846;

Conduit tube(double bore, double length) {
	Conduit c;
	c.pieces[0] = Piece{ bore, length };
	c.count = 1;
	return c;
}

// two syringes' nozzles on a tube, as the lab joins them: 2 mm by 1 cm, 4 mm by 30 cm, 2 mm by 1 cm
Conduit nozzles_on_a_tube() {
	Conduit c;
	c.pieces[0] = Piece{ 2e-3, 10e-3 };
	c.pieces[1] = Piece{ 4e-3, 300e-3 };
	c.pieces[2] = Piece{ 2e-3, 10e-3 };
	c.count = 3;
	return c;
}

constexpr double WATER = 998.2, WATER_MU = 1.002e-3; // kg/m^3, Pa s: 20 C
constexpr double AIR = 0.028964, AIR_MU = 1.82e-5, AIR_GAMMA = 1.4; // kg/mol, Pa s: 20 C

} // namespace

TEST_CASE("conduit: Churchill's friction is 64 / Re laminar, rises through the transition, and falls again turbulent, without a break (Stage 89)") {
	for (const double re : { 1e-6, 1.0, 50.0, 99.9, 100.1, 500.0, 1500.0 }) {
		CAPTURE(re);
		CHECK(friction_factor(re) == doctest::Approx(64.0 / re).epsilon(1e-6));
	}
	CHECK(friction_factor(2000.0) == doctest::Approx(64.0 / 2000.0).epsilon(2e-2));
	CHECK(friction_factor(4000.0) > friction_factor(2300.0)); // the transition's rise
	CHECK(friction_factor(4000.0) == doctest::Approx(0.040).epsilon(0.05)); // Blasius, 0.316 Re^-0.25: 0.0397
	CHECK(friction_factor(1e5) == doctest::Approx(0.316 / std::pow(1e5, 0.25)).epsilon(0.02));
	// what a rate takes rises with it: f Re^2, through every regime
	double before = 0.0;
	for (double re = 1.0; re < 1e7; re *= 1.01) {
		const double takes = friction_factor(re) * re * re;
		REQUIRE(takes > before);
		before = takes;
	}
}

TEST_CASE("conduit: Shah's developing flow is the boundary layer's short and Poiseuille's with 1.25 heads long (Stage 89)") {
	CHECK(entrance_drop(1e-6) == doctest::Approx(13.76 * 1e-3).epsilon(1e-3));
	CHECK(entrance_drop(1e-4) == doctest::Approx(13.76 * 1e-2).epsilon(1e-2));
	CHECK(entrance_drop(1.0) == doctest::Approx(64.0 + 1.25).epsilon(2e-4));
	CHECK(entrance_drop(100.0) == doctest::Approx(6400.0 + 1.25).epsilon(1e-6));
	CHECK(entrance_drop(0.0) == 0.0);
	double before = 0.0;
	for (double x = 1e-7; x < 1e3; x *= 1.02) {
		REQUIRE(entrance_drop(x) > before);
		before = entrance_drop(x);
		REQUIRE(entrance_excess(x) > 0.0); // never under the developed flow's
		REQUIRE(entrance_excess(x) < 1.26);
		REQUIRE(entrance_drop(x) == doctest::Approx(64.0 * x + entrance_excess(x)).epsilon(1e-14));
	}
}

TEST_CASE("conduit: a long thin tube at a low rate is Hagen and Poiseuille's, a liquid and a gas (Stage 89)") {
	// 1 mm by 1 m: a thousand bores long. Its drop is Poiseuille's and 2.25 heads: the 1.25 its flow takes developing,
	// and its jet's own
	const Conduit c = tube(1e-3, 1.0);
	const double a = 0.25 * PI * 1e-6;
	for (const double re : { 0.01, 1.0, 100.0, 1000.0 }) {
		CAPTURE(re);
		const double rate = re * a * WATER_MU / 1e-3; // Re = m D / (A mu)
		const double poiseuille = 32.0 * rate * 1.0 * WATER_MU / (a * 1e-6); // rho dP = 32 m L mu / (A D^2)
		const double head = rate * rate / (2.0 * a * a);
		CHECK(conduit_drive(c, rate, WATER_MU) == doctest::Approx(poiseuille + 2.25 * head).epsilon(3e-4)); // (1.24 of the 1.25 at x = 1)
		CHECK(conduit_drive(c, rate, WATER_MU) == doctest::Approx(poiseuille).epsilon(2.3 * re / 64000.0 + 1e-9)); // (within 1 % below Re 280)
	}
	// a gas the same function: its drive M (P1^2 - P2^2) / (2 R T)
	const double from = 101325.0 + 50.0, to = 101325.0, kelvin = 293.15;
	const double gas = gas_rate(c, from, to, kelvin, AIR, AIR_GAMMA, AIR_MU);
	const double volume = gas / (AIR * 0.5 * (from + to) / (GAS_CONSTANT * kelvin)); // m^3/s at the mean pressure
	CHECK(volume == doctest::Approx(PI * 1e-12 * 50.0 / (128.0 * AIR_MU * 1.0)).epsilon(1e-2)); // pi D^4 dP / (128 mu L)
}

TEST_CASE("conduit: the rate at a drop is the root of the drop at a rate, rising with it, from creeping to turbulent, either way through (Stage 89)") {
	for (const Conduit &c : { tube(2e-3, 10e-3), tube(1e-3, 1.0), nozzles_on_a_tube() }) {
		double before = 0.0;
		for (double drive = 1e-6; drive < 1e12; drive *= 3.7) {
			CAPTURE(drive);
			for (const bool back : { false, true }) {
				const double rate = conduit_rate(c, drive, WATER_MU, back);
				REQUIRE(rate > 0.0);
				REQUIRE(conduit_drive(c, rate, WATER_MU, back) == doctest::Approx(drive).epsilon(1e-10));
				REQUIRE(conduit_drive(c, rate, WATER_MU, back) <= drive * (1.0 + 1e-12)); // never more than the law passes
			}
			const double rate = conduit_rate(c, drive, WATER_MU);
			REQUIRE(rate > before);
			before = rate;
		}
	}
	CHECK(conduit_rate(tube(2e-3, 10e-3), 0.0, WATER_MU) == 0.0);
	CHECK(conduit_rate(tube(2e-3, 10e-3), -1.0, WATER_MU) == 0.0);
	CHECK(conduit_rate(Conduit{}, 1.0, WATER_MU) == 0.0);
	CHECK(conduit_rate(tube(2e-3, 10e-3), 1.0, 0.0) == 0.0);
	// the same either way through what is the same either way; not through what is not (a nozzle onto a wide tube)
	const Conduit same = nozzles_on_a_tube();
	CHECK(conduit_rate(same, 5e7, WATER_MU, true) == doctest::Approx(conduit_rate(same, 5e7, WATER_MU, false)).epsilon(1e-12));
	Conduit lopsided;
	lopsided.pieces[0] = Piece{ 2e-3, 10e-3 };
	lopsided.pieces[1] = Piece{ 6e-3, 20e-3 };
	lopsided.count = 2;
	// out of the nozzle into the wide piece its jet loses (1 - 1/9)^2 of its head, and the slow flow's own is next to
	// nothing: 0.5 + 0.79 heads. The other way the nozzle's contraction loses 0.44 and its jet all of its head into
	// the vessel: 1.44. So it passes more nozzle first
	CHECK(conduit_rate(lopsided, 5e7, WATER_MU, false) > 1.04 * conduit_rate(lopsided, 5e7, WATER_MU, true));
	CHECK(conduit_rate(lopsided, 5e7, WATER_MU, false) < 1.10 * conduit_rate(lopsided, 5e7, WATER_MU, true));
}

TEST_CASE("conduit: water through a syringe's nozzle at a thumb's push runs at a real syringe's pace, where Poiseuille would let through thirty times as much (Stage 89)") {
	// a thumb's 79.8 N on a 100 mL syringe's plunger (37 mm across... its bore 32.6 mm: 8.33 cm^2): 96 kPa less friction;
	// the lab's hand at about 74 kPa over the room's
	const Conduit nozzle = tube(2e-3, 10e-3);
	const double rate = liquid_rate(nozzle, 101325.0 + 74e3, 101325.0, WATER, WATER_MU);
	const double ml_s = rate / WATER * 1e6;
	MESSAGE("a 2 mm nozzle at 74 kPa: " << ml_s << " mL/s of water; Poiseuille's " << PI * 16e-12 * 74e3 / (128.0 * WATER_MU * 10e-3) * 1e6);
	CHECK(ml_s > 25.0);
	CHECK(ml_s < 35.0); // Cd about 0.78 of A sqrt(2 dP / rho) = 38 mL/s
	CHECK(PI * 16e-12 * 74e3 / (128.0 * WATER_MU * 10e-3) * 1e6 > 30.0 * ml_s);
	// through two nozzles on 30 cm of tube: slower still
	const double joined = liquid_rate(nozzles_on_a_tube(), 101325.0 + 74e3, 101325.0, WATER, WATER_MU) / WATER * 1e6;
	MESSAGE("two nozzles on a tube at 74 kPa: " << joined << " mL/s");
	CHECK(joined < 0.8 * ml_s);
	CHECK(joined > 0.5 * ml_s);
	// nothing runs up a pressure, or without a density
	CHECK(liquid_rate(nozzle, 1e5, 1e5, WATER, WATER_MU) == 0.0);
	CHECK(liquid_rate(nozzle, 1e5, 2e5, WATER, WATER_MU) == 0.0);
	CHECK(liquid_rate(nozzle, 2e5, 1e5, 0.0, WATER_MU) == 0.0);
}

TEST_CASE("conduit: a gas blown down through a nozzle is choked at the speed of sound while the pressure ratio is high, then not (Stage 89)") {
	const Conduit nozzle = tube(2e-3, 10e-3);
	const double kelvin = 293.15, a = 0.25 * PI * 4e-6;
	const auto choked = [&](double from) {
		return a * from * std::sqrt(AIR_GAMMA * AIR / (GAS_CONSTANT * kelvin)) * std::pow(2.0 / (AIR_GAMMA + 1.0), (AIR_GAMMA + 1.0) / (2.0 * (AIR_GAMMA - 1.0)));
	};
	// into a vacuum, and into a tenth of it: the same rate, the choked one, in proportion to the giver's pressure
	for (const double from : { 1e5, 5e5 }) {
		CAPTURE(from);
		CHECK(gas_rate(nozzle, from, 0.0, kelvin, AIR, AIR_GAMMA, AIR_MU) == doctest::Approx(choked(from)).epsilon(1e-12));
		CHECK(gas_rate(nozzle, from, 0.1 * from, kelvin, AIR, AIR_GAMMA, AIR_MU) == doctest::Approx(choked(from)).epsilon(1e-12));
	}
	// air at 1 bar through 3.14 mm^2: 0.74 g/s (A P 0.6847 / sqrt(R T / M): 0.0404 A P / sqrt(T) for air)
	CHECK(choked(1e5) == doctest::Approx(0.0404 * a * 1e5 / std::sqrt(kelvin)).epsilon(2e-3));
	// at a small difference: under it, and rising with the giver's pressure and falling with the taker's
	const double slow = gas_rate(nozzle, 101325.0 + 500.0, 101325.0, kelvin, AIR, AIR_GAMMA, AIR_MU);
	CHECK(slow > 0.0);
	CHECK(slow < 0.2 * choked(101325.0 + 500.0));
	CHECK(gas_rate(nozzle, 101325.0 + 600.0, 101325.0, kelvin, AIR, AIR_GAMMA, AIR_MU) > slow);
	CHECK(gas_rate(nozzle, 101325.0 + 500.0, 101325.0 + 100.0, kelvin, AIR, AIR_GAMMA, AIR_MU) < slow);
	double before = 0.0;
	for (double from = 101325.0; from < 1e6; from *= 1.03) { // never falling as the giver's pressure rises, across the choking
		const double rate = gas_rate(nozzle, from, 101325.0, kelvin, AIR, AIR_GAMMA, AIR_MU);
		REQUIRE(rate >= before);
		before = rate;
	}
	CHECK(gas_rate(nozzle, 1e5, 1e5, kelvin, AIR, AIR_GAMMA, AIR_MU) == 0.0);
	CHECK(gas_rate(nozzle, 1e5, 2e5, kelvin, AIR, AIR_GAMMA, AIR_MU) == 0.0);
}

TEST_CASE("conduit: a rate met with what it takes off its own drive: the root of the drop and the give together, less than without, and never more than the drive over the give (Stage 94)") {
	// (a liquid's crossing lowers the surface it leaves and raises the one it joins: the head it runs down falls with
	// the mass crossed, so over a step the rate answers to the head it ends with)
	const Conduit c = nozzles_on_a_tube();
	for (const double drive : { 1e3, 1e5, 1e7, 5e7 }) {
		const double plain = conduit_rate(c, drive, WATER_MU);
		double before = plain;
		for (const double give : { 1e3, 1e6, 1e8, 1e10, 1e13 }) {
			CAPTURE(drive);
			CAPTURE(give);
			const double rate = conduit_rate(c, drive, WATER_MU, false, give);
			REQUIRE(rate > 0.0);
			CHECK(conduit_drive(c, rate, WATER_MU) + give * rate == doctest::Approx(drive).epsilon(1e-10));
			CHECK(rate < before);
			CHECK(rate <= drive / give);
			before = rate;
		}
		// where the give is all there is, the crossing is what levels the two: drive over give
		CHECK(conduit_rate(c, drive, WATER_MU, false, 1e13) == doctest::Approx(drive / 1e13).epsilon(1e-3));
		CHECK(conduit_rate(c, drive, WATER_MU, false, 0.0) == plain);
	}
	CHECK(conduit_rate(c, 1e5, WATER_MU, false, -1.0) == 0.0);
	CHECK(liquid_rate(c, 2e5, 1e5, WATER, WATER_MU, false, 1e6) < liquid_rate(c, 2e5, 1e5, WATER, WATER_MU));
}

TEST_CASE("conduit: what is no conduit is refused: a piece of no bore or no length, too many pieces (Stage 89)") {
	CHECK(good(Conduit{}));
	CHECK(good(nozzles_on_a_tube()));
	Conduit c = tube(0.0, 1.0);
	CHECK_FALSE(good(c));
	c = tube(1e-3, 0.0);
	CHECK_FALSE(good(c));
	c = tube(1e-3, 1.0);
	c.count = MAX_PIECES + 1;
	CHECK_FALSE(good(c));
	c.count = -1;
	CHECK_FALSE(good(c));
	c = tube(std::numeric_limits<double>::infinity(), 1.0);
	CHECK_FALSE(good(c));
}
