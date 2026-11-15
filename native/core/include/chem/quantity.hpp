#pragma once

#include <compare>
#include <cstdint>
#include <limits>

// The conserved quantities are integers (GAME_SPEC §3): an amount of substance
// in quanta of 1e-12 mol, an energy in quanta of 1e-9 J. Moving them is integer
// addition and subtraction, so conservation is an equality, never a tolerance.
namespace chem {

inline constexpr double MOL_PER_QUANTUM = 1e-12;
inline constexpr double JOULE_PER_QUANTUM = 1e-9;

struct Amount {
	int64_t quanta = 0;
	constexpr auto operator<=>(const Amount &) const = default;
	constexpr double mol() const { return static_cast<double>(quanta) * MOL_PER_QUANTUM; }
};

struct Energy {
	int64_t quanta = 0;
	constexpr auto operator<=>(const Energy &) const = default;
	constexpr double joule() const { return static_cast<double>(quanta) * JOULE_PER_QUANTUM; }
};

// true when a + b does not fit an int64
constexpr bool add_overflows(int64_t a, int64_t b) {
	constexpr int64_t hi = std::numeric_limits<int64_t>::max();
	constexpr int64_t lo = std::numeric_limits<int64_t>::min();
	return (b > 0 && a > hi - b) || (b < 0 && a < lo - b);
}

// true when a * b does not fit an int64
constexpr bool mul_overflows(int64_t a, int64_t b) {
	constexpr int64_t hi = std::numeric_limits<int64_t>::max();
	constexpr int64_t lo = std::numeric_limits<int64_t>::min();
	if (a == 0 || b == 0) {
		return false;
	}
	if (a > 0) {
		return b > 0 ? a > hi / b : b < lo / a;
	}
	return b > 0 ? a < lo / b : b < hi / a;
}

// The nearest whole number of quanta to a float64 count (ties away from zero);
// for initial conditions and, from Stage 3, for rates. `ok` is false when the
// value is not finite or out of range (the result is then 0).
constexpr int64_t round_quanta(double count, bool &ok) {
	ok = count == count && count > -9.0e18 && count < 9.0e18;
	if (!ok) {
		return 0;
	}
	return static_cast<int64_t>(count >= 0.0 ? count + 0.5 : count - 0.5);
}

constexpr Amount amount_from_mol(double mol, bool &ok) {
	return Amount{ round_quanta(mol / MOL_PER_QUANTUM, ok) };
}

constexpr Energy energy_from_joule(double joule, bool &ok) {
	return Energy{ round_quanta(joule / JOULE_PER_QUANTUM, ok) };
}

} // namespace chem
