// chem_tool study spectrum: how many bands the renderer needs (GAME_SPEC §12.1).
// Every colour the demo can show, by the 1 nm reference and by n bands; the
// CIEDE2000 difference, for several ways of cutting the spectrum and two ways
// of carrying an absorber in a band.
#include "lab/colour.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

using chem::optics::Bands;

double score(const lab::BandError &e) {
	// the worst colour first, the mean as far as the threshold weighs it
	return std::max(e.worst / lab::BAND_ERROR_WORST, e.mean / lab::BAND_ERROR_MEAN) + 0.01 * e.mean;
}

std::vector<int> cuts_of(const Bands &bands) {
	std::vector<int> cuts;
	for (int k = 1; k < bands.count; ++k) {
		cuts.push_back(chem::optics::FIRST_NM + bands.edge[static_cast<size_t>(k)]);
	}
	return cuts;
}

// Cuts moved one at a time, by smaller and smaller steps, while the error falls.
Bands fitted(const Bands &start, lab::BandModel model) {
	std::vector<int> cuts = cuts_of(start);
	double best = score(lab::band_error(start, model));
	for (const int step : { 16, 8, 4, 2, 1 }) {
		for (int sweep = 0; sweep < 12; ++sweep) {
			bool moved = false;
			for (size_t k = 0; k < cuts.size(); ++k) {
				for (const int direction : { -1, 1 }) {
					const int to = cuts[k] + direction * step;
					const int low = k == 0 ? 381 : cuts[k - 1] + 1, high = k + 1 == cuts.size() ? 779 : cuts[k + 1] - 1;
					if (to < low || to > high) {
						continue;
					}
					const int from = cuts[k];
					cuts[k] = to;
					const double s = score(lab::band_error(chem::optics::bands_cut_at(cuts), model));
					if (s < best) {
						best = s;
						moved = true;
					} else {
						cuts[k] = from;
					}
				}
			}
			if (!moved) {
				break;
			}
		}
	}
	return chem::optics::bands_cut_at(cuts);
}

bool passes(const lab::BandError &e) {
	return e.worst <= lab::BAND_ERROR_WORST && e.mean <= lab::BAND_ERROR_MEAN;
}

} // namespace

int spectrum_study() {
	std::printf("The colour of everything the demo can show (54 solutions, 28 solids bare and under solution, 10 through\n"
				"the balloon's rubber, 18 glowing bodies), by n bands against the 1 nm reference: CIEDE2000, where about 1\n"
				"is just noticeable. Threshold: worst <= %.1f and mean <= %.1f (*).\n"
				"Cuts: even = equal widths over 400-700 nm; weight = equal shares of what the eye sees; fitted = moved to\n"
				"lower the error. A band carries each absorber's mean, or its mean and its tilt across the band.\n\n",
			lab::BAND_ERROR_WORST, lab::BAND_ERROR_MEAN);
	std::printf("%3s | %-47s | %-47s\n", "", "mean only", "mean and tilt");
	std::printf("%3s | %-15s %-15s %-15s | %-15s %-15s %-15s\n", "n", "even", "weight", "fitted", "even", "weight", "fitted");
	std::printf("%3s | %7s %6s  %7s %6s  %7s %6s  | %7s %6s  %7s %6s  %7s %6s\n", "", "worst", "mean", "worst", "mean", "worst", "mean", "worst", "mean", "worst", "mean", "worst", "mean");
	for (int n = 3; n <= chem::optics::MAX_BANDS; n += n < 16 ? 1 : 4) {
		std::printf("%3d |", n);
		for (const lab::BandModel model : { lab::BandModel::Mean, lab::BandModel::Tilted }) {
			const Bands even = chem::optics::bands_uniform(n, 400, 700), weight = chem::optics::bands_equal_weight(n);
			const lab::BandError a = lab::band_error(even, model), b = lab::band_error(weight, model);
			const Bands fit = fitted(score(a) < score(b) ? even : weight, model);
			const lab::BandError c = lab::band_error(fit, model);
			for (const lab::BandError &e : { a, b, c }) {
				std::printf(" %7.3f %6.3f%s", e.worst, e.mean, passes(e) ? "*" : " ");
			}
			std::printf(" |");
			if (passes(c) && n <= 16) {
				std::printf(" cuts:");
				for (const int cut : cuts_of(fit)) {
					std::printf(" %d", cut);
				}
			}
		}
		std::printf("\n");
	}

	const Bands &game = chem::optics::game_bands();
	lab::BandError e = lab::band_error(game, lab::BandModel::Mean, true);
	std::printf("\nThe game renders with %d bands (weight cuts, mean only): worst %.3f (%s), mean %.3f; 10 nm lines %.2f.\n", game.count, e.worst, e.worst_case.c_str(), e.mean, e.worst_narrow);
	for (int k = 0; k < game.count; ++k) {
		std::printf("  band %d: %d-%d nm, centre %.1f nm\n", k, 380 + game.edge[static_cast<size_t>(k)], 380 + game.edge[static_cast<size_t>(k) + 1] - 1, game.centre_nm[static_cast<size_t>(k)]);
	}
	std::sort(e.all.rbegin(), e.all.rend());
	std::printf("its five worst colours:\n");
	for (size_t i = 0; i < 5 && i < e.all.size(); ++i) {
		std::printf("  %.3f  %s\n", e.all[i].first, e.all[i].second.c_str());
	}
	return passes(e) ? 0 : 1;
}
