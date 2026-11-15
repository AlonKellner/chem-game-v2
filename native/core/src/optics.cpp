#include "chem/optics.hpp"

namespace chem::optics {

namespace {

// the weight of each nanometre: what the eye and the illuminant make of it
double weight(int i) {
	return (xbar()[static_cast<size_t>(i)] + ybar()[static_cast<size_t>(i)] + zbar()[static_cast<size_t>(i)]) * d65()[static_cast<size_t>(i)];
}

// the illuminant's own Y: the scale on which its white is 1
double white() {
	static const double y = [] {
		double sum = 0.0;
		for (size_t i = 0; i < COUNT; ++i) {
			sum += ybar()[i] * d65()[i];
		}
		return sum;
	}();
	return y;
}

Bands finish(Bands bands) {
	for (int k = 0; k < bands.count; ++k) {
		double total = 0.0, moment = 0.0;
		Xyz lit, plain;
		for (int i = bands.edge[static_cast<size_t>(k)]; i < bands.edge[static_cast<size_t>(k) + 1]; ++i) {
			const auto u = static_cast<size_t>(i);
			lit.x += xbar()[u] * d65()[u];
			lit.y += ybar()[u] * d65()[u];
			lit.z += zbar()[u] * d65()[u];
			plain.x += xbar()[u];
			plain.y += ybar()[u];
			plain.z += zbar()[u];
			total += weight(i);
			moment += weight(i) * static_cast<double>(FIRST_NM + i);
		}
		const double w = white();
		bands.lit[static_cast<size_t>(k)] = Xyz{ lit.x / w, lit.y / w, lit.z / w };
		bands.plain[static_cast<size_t>(k)] = Xyz{ plain.x / w, plain.y / w, plain.z / w };
		bands.centre_nm[static_cast<size_t>(k)] = total > 0.0 ? moment / total : 0.0;
	}
	return bands;
}

} // namespace

Xyz colour(const Spectrum &factor) {
	Xyz out;
	for (size_t i = 0; i < COUNT; ++i) {
		out.x += xbar()[i] * d65()[i] * factor[i];
		out.y += ybar()[i] * d65()[i] * factor[i];
		out.z += zbar()[i] * d65()[i] * factor[i];
	}
	const double w = white();
	return Xyz{ out.x / w, out.y / w, out.z / w };
}

Xyz colour_emitted(const Spectrum &radiance) {
	Xyz out;
	for (size_t i = 0; i < COUNT; ++i) {
		out.x += xbar()[i] * radiance[i];
		out.y += ybar()[i] * radiance[i];
		out.z += zbar()[i] * radiance[i];
	}
	const double w = white();
	return Xyz{ out.x / w, out.y / w, out.z / w };
}

Bands bands_uniform(int count, int from_nm, int to_nm) {
	Bands bands;
	bands.count = count < 1 ? 1 : count > MAX_BANDS ? MAX_BANDS : count;
	const int lo = from_nm - FIRST_NM, hi = to_nm - FIRST_NM + 1;
	for (int k = 0; k <= bands.count; ++k) {
		bands.edge[static_cast<size_t>(k)] = lo + (hi - lo) * k / bands.count;
	}
	bands.edge[0] = 0; // the tails go to the end bands
	bands.edge[static_cast<size_t>(bands.count)] = COUNT;
	return finish(bands);
}

Bands bands_equal_weight(int count) {
	Bands bands;
	bands.count = count < 1 ? 1 : count > MAX_BANDS ? MAX_BANDS : count;
	double total = 0.0;
	for (int i = 0; i < COUNT; ++i) {
		total += weight(i);
	}
	double running = 0.0;
	int next = 1;
	bands.edge[0] = 0;
	for (int i = 0; i < COUNT && next < bands.count; ++i) {
		running += weight(i);
		if (running >= total * static_cast<double>(next) / static_cast<double>(bands.count)) {
			bands.edge[static_cast<size_t>(next++)] = i + 1;
		}
	}
	for (; next < bands.count; ++next) {
		bands.edge[static_cast<size_t>(next)] = COUNT;
	}
	bands.edge[static_cast<size_t>(bands.count)] = COUNT;
	return finish(bands);
}

Bands bands_cut_at(std::span<const int> cuts_nm) {
	Bands bands;
	bands.count = static_cast<int>(cuts_nm.size()) + 1 > MAX_BANDS ? MAX_BANDS : static_cast<int>(cuts_nm.size()) + 1;
	bands.edge[0] = 0;
	for (int k = 1; k < bands.count; ++k) {
		bands.edge[static_cast<size_t>(k)] = cuts_nm[static_cast<size_t>(k) - 1] - FIRST_NM;
	}
	bands.edge[static_cast<size_t>(bands.count)] = COUNT;
	return finish(bands);
}

void band_averages(const Bands &bands, const Spectrum &s, std::span<double> out, bool emitted) {
	for (int k = 0; k < bands.count && static_cast<size_t>(k) < out.size(); ++k) {
		double total = 0.0, sum = 0.0;
		for (int i = bands.edge[static_cast<size_t>(k)]; i < bands.edge[static_cast<size_t>(k) + 1]; ++i) {
			const auto u = static_cast<size_t>(i);
			const double w = emitted ? xbar()[u] + ybar()[u] + zbar()[u] : weight(i);
			total += w;
			sum += w * s[u];
		}
		out[static_cast<size_t>(k)] = total > 0.0 ? sum / total : 0.0;
	}
}

Xyz colour(const Bands &bands, std::span<const double> factor) {
	Xyz out;
	for (int k = 0; k < bands.count && static_cast<size_t>(k) < factor.size(); ++k) {
		const Xyz &b = bands.lit[static_cast<size_t>(k)];
		out.x += b.x * factor[static_cast<size_t>(k)];
		out.y += b.y * factor[static_cast<size_t>(k)];
		out.z += b.z * factor[static_cast<size_t>(k)];
	}
	return out;
}

Xyz colour_emitted(const Bands &bands, std::span<const double> radiance) {
	Xyz out;
	for (int k = 0; k < bands.count && static_cast<size_t>(k) < radiance.size(); ++k) {
		// unit radiance over the band: its plain colour is the sum over its nanometres
		const Xyz &b = bands.plain[static_cast<size_t>(k)];
		out.x += b.x * radiance[static_cast<size_t>(k)];
		out.y += b.y * radiance[static_cast<size_t>(k)];
		out.z += b.z * radiance[static_cast<size_t>(k)];
	}
	return out;
}

const Bands &game_bands() {
	// chosen by measurement (chem_tool study spectrum, docs/TDD_10): the fewest
	// bands within the threshold whose cuts are not fitted to the test colours
	static const Bands bands = bands_equal_weight(GAME_BANDS);
	return bands;
}

} // namespace chem::optics
