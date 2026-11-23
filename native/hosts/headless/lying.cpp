// chem_tool study tilt, chem_tool study vortex: what lies in a vessel (GAME_SPEC §11, Stages 30 to 32;
// lab/contents.hpp), printed against its closed forms. Pure geometry and its two laws: no world.
#include "lab/contents.hpp"
#include "lab/glassware.hpp"

#include <cmath>
#include <cstdio>

namespace {

constexpr double PI = 3.14159265358979323846;
namespace C = lab::contents;

struct Made {
	const char *kind;
	double ml;
};
const Made MADE[] = { { "beaker", 250.0 }, { "conical_flask", 250.0 }, { "bottle", 500.0 } };

} // namespace

// Each vessel half full, turned from upright to its full tilt: what it holds below its lip, how far its liquid
// stands over the lip, and what runs out of its mouth. Checked: upright it holds its capacity, the more it is
// turned the less, nothing at the full tilt; and a small head runs as over a sharp-crested weir.
int tilt_study() {
	bool ok = true;
	for (const Made &m : MADE) {
		const lab::glass::Vessel *g = lab::glass::find(m.kind, m.ml);
		if (g == nullptr) {
			std::fprintf(stderr, "chem_tool study tilt: no %s of %.0f mL\n", m.kind, m.ml);
			return 1;
		}
		const double full = C::full_tilt(*g, 1), liquid = 0.5 * g->capacity();
		std::printf("%s, %.0f mL (holds %.1f to its brim; its mouth %.2f cm across), with %.1f mL in it; its full tilt %.1f degrees\n", m.kind, m.ml, g->capacity(), 2.0 * g->mouth(), liquid,
				full * 180.0 / PI);
		std::printf("  %8s %14s %12s %12s\n", "turned", "holds to lip", "over lip", "runs");
		double last = g->capacity() * (1.0 + 1e-9);
		for (int k = 0; k <= 10; ++k) {
			const double turn = full * k / 10.0, holds = C::held_at(*g, turn, 1);
			const C::Lying l = C::lying(*g, turn, liquid, 0.0, 0.0, 1);
			const double runs = C::over_brim(std::fabs(l.lip_x), turn, l.head);
			std::printf("  %6.1f d %11.2f mL %9.2f mm %7.2f mL/s\n", turn * 180.0 / PI, holds, 10.0 * l.head, runs);
			ok = ok && holds <= last && C::check_H1(*g, l, liquid, 0.0).empty() && C::check_H2(l).empty();
			ok = ok && ((l.head > 0.0) == (liquid > holds * (1.0 + 1e-9) || (k == 10 && liquid > 0.0)));
			last = holds;
		}
		ok = ok && std::fabs(C::held_at(*g, 0.0, 1) - g->capacity()) < 1e-6 * g->capacity() && C::held_at(*g, full, 1) < 1e-6 * g->capacity();
		// a small head over a lip turned 60 degrees: the weir's closed form
		const double brim = g->mouth(), head = 0.02, weir = C::WEIR_CD * std::sqrt(2.0 * C::GRAVITY) * (PI / 4.0) * std::sqrt(2.0 * brim / std::sin(PI / 3.0)) * head * head;
		const double law = C::over_brim(brim, PI / 3.0, head);
		std::printf("  0.2 mm over its lip at 60 degrees: %.5f mL/s (a sharp-crested weir: %.5f)\n\n", law, weir);
		ok = ok && std::fabs(law - weir) < 0.01 * weir;
	}
	std::printf("%s\n", ok ? "every vessel holds less the more it is turned, nothing at its full tilt, and pours as a weir under a small head" : "A CHECK FAILED");
	return ok ? 0 : 1;
}

// 200 mL of water in a 250 mL beaker, turning with more and more energy: its vortex, and the law it stands
// for, E = (M g / A) V. Then a liquid twice as heavy, and a shallow one.
int vortex_study() {
	const lab::glass::Vessel *g = lab::glass::find("beaker", 250.0);
	if (g == nullptr) {
		return 1;
	}
	bool ok = true;
	const auto table = [&](const char *what, double liquid, double mass) {
		const C::Swirl most = C::swirl(*g, 0.0, liquid, 0.0, mass, 0.0);
		std::printf("%s: %.0f mL, %.0f g, %.2f cm deep; its turning holds %.2f mJ at the most\n", what, liquid, mass, most.depth, most.most * 1000.0);
		std::printf("  %10s %8s %12s %14s %14s\n", "energy", "share", "cone deep", "displaces", "(M g / A) V");
		for (const double mj : { 1.0, 5.0, 15.0, 40.0, 100.0 }) {
			const double energy = mj * 1e-3;
			const C::Swirl s = C::swirl(*g, 0.0, liquid, 0.0, mass, energy);
			const C::Lying l = C::lying(*g, 0.0, liquid, 0.0, s.share, 1);
			const double law = mass * 1e-3 * C::GRAVITY * 1e-2 / (l.surface_area * 1e-4) * l.vortex.volume() * 1e-6;
			std::printf("  %7.1f mJ %7.1f%% %9.2f cm %11.2f mL %11.2f mJ%s\n", mj, 100.0 * s.share, l.vortex.height, l.vortex.volume(), law * 1000.0, energy > most.most ? "  (more than it holds)" : "");
			ok = ok && C::check_H1(*g, l, liquid, 0.0).empty() && C::check_H2(l).empty();
			ok = ok && (energy > most.most ? s.share == 1.0 : std::fabs(law - energy) < 0.01 * energy);
		}
		std::printf("\n");
		return most.most;
	};
	const double water = table("water", 200.0, 200.0);
	const double heavy = table("a liquid twice as heavy", 200.0, 400.0);
	const double shallow = table("a little water", 40.0, 40.0);
	ok = ok && std::fabs(heavy - 2.0 * water) < 1e-9 * water && shallow < water;
	// the game's stirrer keeps up P tau = 15 mJ
	std::printf("the game's stirrer (0.01 W; a liquid keeps turning 1.5 s) keeps up 15 mJ: a vortex %.0f%% of the largest that fits in the 200 mL, and at the glass in the 40 mL\n",
			100.0 * C::swirl(*g, 0.0, 200.0, 0.0, 200.0, 0.015).share);
	std::printf("%s\n", ok ? "the cone drawn displaces what its energy says, E = (M g / A) V, to a part in a hundred" : "A CHECK FAILED");
	return ok ? 0 : 1;
}
