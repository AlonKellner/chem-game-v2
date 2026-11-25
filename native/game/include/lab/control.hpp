#pragma once

#include <vector>

// A controller's tuning (GAME_SPEC §13.3, Stage 26): what its trial found, the
// process fitted to it, and the PI gains from Skogestad's SIMC rules
// (S. Skogestad, "Simple analytic rules for model reduction and PID controller
// tuning", J. Process Control 13 (2003) 291-309; docs/DATA_SOURCES.md).
// Engine-free and pure.
namespace lab {

// A process as the trial saw it: a first-order one with a delay, its gain
// `gain` (the quantity's unit for the whole level), its time constant `tau`
// and delay `theta` (s); or an integrating one (`integrating`), `gain` its
// slope (the unit per second for the whole level), `tau` unused.
struct ProcessModel {
	double gain = 0.0, tau = 0.0, theta = 0.0;
	bool integrating = false;
	bool operator==(const ProcessModel &) const = default;
};

// The least response a trial counts (Stage 50), in the sensed quantity's own unit (a microkelvin, a microgram): under
// it nothing moved. An open vessel's gas exchanges with the room's air, so a control there reaches every open vessel by
// a trace (7e-10 K, measured), where a real one reaches its own by thousandths.
inline constexpr double LEAST_RESPONSE = 1e-6;
// The process that best fits a step's response `r` (the reading with every
// control full on, less the reading with them off) at times `t` (s, from the
// step, increasing): least squares over a grid of delays and time constants,
// and the integrating process beside them. False when nothing moved.
bool fit_process(const std::vector<double> &t, const std::vector<double> &r, ProcessModel &out);

// The speeds a player chooses, and the closed loop's time each gives (SIMC's
// tau_c): gentle the process's own time, normal a quarter of it, fast a
// twentieth; never under the delay (SIMC's tight choice is the delay itself).
// `span` is the process's time: its tau, or the trial's length for an
// integrating one.
enum class Speed : int { Gentle = 0, Normal = 1, Fast = 2 };
double closed_loop_time(const ProcessModel &m, Speed speed, double span);

// SIMC's PI gains for a closed loop of time `tau_c`: Kc = tau / (K (tau_c +
// theta)), Ti = min(tau, 4 (tau_c + theta)); for an integrating process Kc = 1
// / (k' (tau_c + theta)), Ti = 4 (tau_c + theta). The delay is taken as at
// least `least_delay` (the controller's own: a step to read, a step to act).
struct PiGains {
	double kc = 0.0, ti = 0.0;
};
PiGains simc(const ProcessModel &m, double tau_c, double least_delay);

// The loop's gain and phase margins (the gain by which the loop may grow, and
// the phase in degrees by which it may lag, before it oscillates), of the PI
// controller on the process: how robust the tuning is (SIMC's own figures, for
// the gate test).
struct Margins {
	double gain = 0.0, phase = 0.0;
};
Margins margins(const ProcessModel &m, const PiGains &g);

} // namespace lab
