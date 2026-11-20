// The tracer and the refraction maps (GAME_SPEC §12.3, Stage 20).
#include "lab/optics_trace.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace lab::trace {

namespace {

double dot(const Vec3 &a, const Vec3 &b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 along(const Vec3 &o, const Vec3 &u, double t) {
	return Vec3{ o.x + t * u.x, o.y + t * u.y, o.z + t * u.z };
}

bool within(const Patch &p, const Vec3 &at) {
	constexpr double SLACK = 1.0e-9;
	return at.x >= p.lo.x - SLACK && at.x <= p.hi.x + SLACK && at.y >= p.lo.y - SLACK && at.y <= p.hi.y + SLACK && at.z >= p.lo.z - SLACK && at.z <= p.hi.z + SLACK;
}

} // namespace

Landing trace(const Shape &shape, Vec3 o, Vec3 u, double wall, const std::array<double, MAX_MEDIA> &index) {
	Landing out;
	int here = 0;
	constexpr int MOST = 32;
	constexpr double NEAR = 1.0e-9; // a surface just left is not met again
	for (; out.meetings < MOST;) {
		double best = 1.0e300;
		int hit = -1;
		for (size_t i = 0; i < shape.patches.size(); ++i) {
			const Patch &p = shape.patches[i];
			// f(o + t u) = A t^2 + B t + C
			const double a = p.a.x * u.x * u.x + p.a.y * u.y * u.y + p.a.z * u.z * u.z;
			const double b = 2.0 * (p.a.x * o.x * u.x + p.a.y * o.y * u.y + p.a.z * o.z * u.z) + dot(p.l, u);
			const double c = p.a.x * o.x * o.x + p.a.y * o.y * o.y + p.a.z * o.z * o.z + dot(p.l, o) + p.c;
			double roots[2];
			int count = 0;
			if (std::fabs(a) < 1.0e-14) {
				if (std::fabs(b) > 1.0e-14) {
					roots[count++] = -c / b;
				}
			} else {
				const double disc = b * b - 4.0 * a * c;
				if (disc >= 0.0) {
					// the stable pair: no cancellation in the smaller root
					const double q = -0.5 * (b + (b < 0.0 ? -1.0 : 1.0) * std::sqrt(disc));
					roots[count++] = q / a;
					if (q != 0.0) {
						roots[count++] = c / q;
					}
				}
			}
			for (int k = 0; k < count; ++k) {
				if (roots[k] > NEAR && roots[k] < best && within(p, along(o, u, roots[k]))) {
					best = roots[k];
					hit = static_cast<int>(i);
				}
			}
		}
		if (hit < 0) {
			break;
		}
		const Patch &p = shape.patches[static_cast<size_t>(hit)];
		out.chord[static_cast<size_t>(here)] += best;
		o = along(o, u, best);
		++out.meetings;
		// the surface's normal, and which medium is beyond it
		Vec3 n{ 2.0 * p.a.x * o.x + p.l.x, 2.0 * p.a.y * o.y + p.l.y, 2.0 * p.a.z * o.z + p.l.z };
		const double length = std::sqrt(dot(n, n));
		if (!(length > 0.0)) {
			continue;
		}
		n = Vec3{ n.x / length, n.y / length, n.z / length };
		const bool rising = dot(n, u) > 0.0; // from f < 0 to f > 0
		const int there = rising ? p.above : p.below;
		if (there == here) {
			continue; // grazing, or a surface between two parts of one medium
		}
		if (rising) {
			n = Vec3{ -n.x, -n.y, -n.z }; // against the ray
		}
		const double n1 = index[static_cast<size_t>(here)], n2 = index[static_cast<size_t>(there)];
		const double cos_i = -dot(n, u), eta = n1 / n2, sin2 = eta * eta * (1.0 - cos_i * cos_i);
		if (sin2 >= 1.0) {
			// turned back, all of it
			u = Vec3{ u.x + 2.0 * cos_i * n.x, u.y + 2.0 * cos_i * n.y, u.z + 2.0 * cos_i * n.z };
			out.route = out.route * 31u + static_cast<uint32_t>(2 * (hit + 1) + 1);
			continue;
		}
		const double cos_t = std::sqrt(1.0 - sin2);
		const double rs = (n1 * cos_i - n2 * cos_t) / (n1 * cos_i + n2 * cos_t), rp = (n1 * cos_t - n2 * cos_i) / (n1 * cos_t + n2 * cos_i);
		out.pass *= 1.0 - 0.5 * (rs * rs + rp * rp);
		const double k = eta * cos_i - cos_t;
		u = Vec3{ eta * u.x + k * n.x, eta * u.y + k * n.y, eta * u.z + k * n.z };
		here = there;
		out.route = out.route * 31u + static_cast<uint32_t>(2 * (hit + 1));
	}
	if (here != 0) {
		out.pass = 0.0; // still inside after every meeting allowed: nothing of the wall comes this way
		out.route = out.route * 31u + 7u;
	}
	out.lands = here == 0 && u.z > 0.05;
	if (out.lands) {
		const double t = (wall - o.z) / u.z;
		out.x = o.x + t * u.x;
		out.y = o.y + t * u.y;
	} else {
		// it leaves sideways or back toward the eye: far off the wall, on the side it goes
		out.x = u.x < 0.0 ? -1.0e5 : 1.0e5;
		out.y = o.y;
		out.route = out.route * 31u + 5u;
	}
	return out;
}

Shape cylinder(double outer, double glass) {
	Shape s;
	Patch shell;
	shell.a = Vec3{ 1.0, 0.0, 1.0 };
	shell.c = -outer * outer;
	shell.below = glass > 0.0 ? GLASS : CORE;
	shell.above = 0;
	s.patches.push_back(shell);
	if (glass > 0.0 && glass < outer) {
		Patch inner;
		inner.a = Vec3{ 1.0, 0.0, 1.0 };
		inner.c = -(outer - glass) * (outer - glass);
		inner.below = CORE;
		inner.above = GLASS;
		s.patches.push_back(inner);
	}
	return s;
}

Shape prism(const double corners[3][2]) {
	Shape s;
	const double cx = (corners[0][0] + corners[1][0] + corners[2][0]) / 3.0, cz = (corners[0][1] + corners[1][1] + corners[2][1]) / 3.0;
	for (int e = 0; e < 3; ++e) {
		const int f = (e + 1) % 3;
		const double ax = corners[e][0], az = corners[e][1], bx = corners[f][0], bz = corners[f][1];
		// the face's plane: n . (p - a) = 0, n across the edge
		Patch face;
		face.l = Vec3{ bz - az, 0.0, -(bx - ax) };
		face.c = -(face.l.x * ax + face.l.z * az);
		const bool centre_below = face.l.x * cx + face.l.z * cz + face.c < 0.0;
		face.below = centre_below ? GLASS : 0;
		face.above = centre_below ? 0 : GLASS;
		face.lo = Vec3{ std::min(ax, bx), -1.0e300, std::min(az, bz) };
		face.hi = Vec3{ std::max(ax, bx), 1.0e300, std::max(az, bz) };
		s.patches.push_back(face);
	}
	return s;
}

Shape ball(double radius) {
	Shape s;
	Patch skin;
	skin.a = Vec3{ 1.0, 1.0, 1.0 };
	skin.c = -radius * radius;
	skin.below = GLASS;
	skin.above = 0;
	s.patches.push_back(skin);
	return s;
}

// ---- maps

namespace {

double index_at(const float *row, double nm) {
	if (row == nullptr) {
		return 1.0;
	}
	const double x = std::min(std::max(nm - 380.0, 0.0), 400.0);
	const int i = std::min(static_cast<int>(x), 399);
	const double f = x - i;
	return row[i] * (1.0 - f) + row[i + 1] * f;
}

// whether the core is liquid: with two media the first is the liquid's, with one it is air's
bool liquid(const MapSpec &spec, int medium) {
	return spec.media == 2 && medium == 0;
}

} // namespace

int Map::samples() const {
	return coarse * MAP_FINE + 1;
}

MapRay map_ray(const MapSpec &spec, double x, double nm, int medium) {
	std::array<double, MAX_MEDIA> index{ 1.0, index_at(spec.glass_index, nm), liquid(spec, medium) ? index_at(spec.liquid_index, nm) : 1.0, 1.0 };
	const Landing l = trace(spec.shape, Vec3{ x * spec.cm_per_pixel, 0.0, -1000.0 }, Vec3{ 0.0, 0.0, 1.0 }, spec.wall_cm, index);
	MapRay out;
	out.landing = l.lands ? l.x / spec.cm_per_pixel : l.x;
	out.pass = l.pass;
	out.chord = liquid(spec, medium) ? l.chord[CORE] : 0.0;
	out.route = l.route;
	return out;
}

Map build_map(const MapSpec &spec) {
	Map map;
	map.media = spec.media;
	map.left = std::floor(spec.left);
	map.coarse = std::max(1, static_cast<int>(std::ceil(spec.right - map.left)));
	const int n = map.samples();

	// the rows: the fewest, evenly spaced, that keep the landing between two of them straight in
	// wavelength, tried on rays across the body at every 3.125 nm
	constexpr int FINEST = 129;
	map.rows = 17;
	{
		constexpr int PROBES = 33;
		std::vector<MapRay> probe(static_cast<size_t>(FINEST));
		for (int p = 0; p < PROBES; ++p) {
			const double x = spec.left + (spec.right - spec.left) * (p + 0.5) / PROBES;
			for (int medium = 0; medium < spec.media; ++medium) {
				for (int k = 0; k < FINEST; ++k) {
					probe[static_cast<size_t>(k)] = map_ray(spec, x, 380.0 + 400.0 * k / (FINEST - 1), medium);
					++map.rays;
				}
				// how fast the landing moves across the body here: an error on the wall shifts the image by
				// that much less on the screen
				const MapRay beside = map_ray(spec, x + 1.0 / MAP_FINE, 580.0, medium);
				++map.rays;
				const double slope = std::max(1.0, std::fabs(beside.landing - probe[FINEST / 2].landing) * MAP_FINE);
				for (; map.rows < FINEST;) {
					const int stride = (FINEST - 1) / (map.rows - 1);
					bool straight = true;
					for (int k = 0; k + stride < FINEST && straight; k += stride) {
						const MapRay &a = probe[static_cast<size_t>(k)], &m = probe[static_cast<size_t>(k + stride / 2)], &b = probe[static_cast<size_t>(k + stride)];
						// the rows are read by the cubic through four of them (map_read): the row before and the one after
						const bool before = k - stride >= 0 && probe[static_cast<size_t>(k - stride)].route == m.route;
						const bool after = k + 2 * stride < FINEST && probe[static_cast<size_t>(k + 2 * stride)].route == m.route;
						const double p0 = before ? probe[static_cast<size_t>(k - stride)].landing : 2.0 * a.landing - b.landing;
						const double p3 = after ? probe[static_cast<size_t>(k + 2 * stride)].landing : 2.0 * b.landing - a.landing;
						const double read = (-p0 + 9.0 * a.landing + 9.0 * b.landing - p3) / 16.0;
						straight = a.route != m.route || b.route != m.route || m.pass < 0.02 || std::fabs(m.landing - read) <= MAP_LANDING_TOLERANCE * slope;
					}
					if (straight) {
						break;
					}
					map.rows = (map.rows - 1) * 2 + 1;
				}
			}
		}
	}

	// the cells: traced where the map bends, filled along the straight line where it does not
	const int per = map.media * map.rows; // rays a sample
	std::vector<MapRay> rays(static_cast<size_t>(n) * static_cast<size_t>(per));
	std::vector<char> known(static_cast<size_t>(n), 0);
	const auto shoot = [&](int i) {
		if (known[static_cast<size_t>(i)]) {
			return;
		}
		for (int medium = 0; medium < map.media; ++medium) {
			for (int row = 0; row < map.rows; ++row) {
				rays[static_cast<size_t>(i) * static_cast<size_t>(per) + static_cast<size_t>(medium * map.rows + row)] =
						map_ray(spec, map.left + static_cast<double>(i) / MAP_FINE, 380.0 + 400.0 * row / (map.rows - 1), medium);
				++map.rays;
			}
		}
		known[static_cast<size_t>(i)] = 1;
	};
	const auto ray = [&](int i, int k) -> MapRay & { return rays[static_cast<size_t>(i) * static_cast<size_t>(per) + static_cast<size_t>(k)]; };
	map.level.assign(static_cast<size_t>(map.coarse), 1.0f);
	// [i0, i1] in samples, a power of two wide: straight if the middle ray is where the ends put it
	const auto refine = [&](const auto &self, int i0, int i1, int cell) -> void {
		shoot(i0);
		shoot(i1);
		const int width = i1 - i0;
		if (width == 1) {
			map.level[static_cast<size_t>(cell)] = static_cast<float>(MAP_FINE);
			return;
		}
		const int mid = i0 + width / 2;
		shoot(mid);
		bool straight = true;
		for (int k = 0; k < per && straight; ++k) {
			const MapRay &a = ray(i0, k), &m = ray(mid, k), &b = ray(i1, k);
			// Straight: the middle ray lands where the ends put it. Within MAP_LANDING_TOLERANCE as a shift of the
			// image on the screen (the error on the wall over the map's slope across the part); and so that the
			// part's two halves have slopes alike within MAP_SLOPE_TOLERANCE, for a thin line's image is as bright
			// as the slope where it lands is small. (Where rays cross, the slope passes through nothing: there
			// the first alone, down to the finest part.)
			const double spread = std::fabs(b.landing - a.landing), slope = std::max(1.0, spread * MAP_FINE / width);
			const double allowed = std::min(MAP_LANDING_TOLERANCE * slope, std::max(0.25 * MAP_SLOPE_TOLERANCE * spread, 0.0005));
			straight = a.route == m.route && b.route == m.route && std::fabs(m.landing - 0.5 * (a.landing + b.landing)) <= allowed &&
					   std::fabs(m.pass - 0.5 * (a.pass + b.pass)) <= MAP_PASS_TOLERANCE &&
					   std::fabs(m.chord * m.chord - 0.5 * (a.chord * a.chord + b.chord * b.chord)) <= 0.01 * std::max(1.0, m.chord * m.chord);
		}
		if (!straight) {
			self(self, i0, mid, cell);
			self(self, mid, i1, cell);
			return;
		}
		map.level[static_cast<size_t>(cell)] = std::max(map.level[static_cast<size_t>(cell)], static_cast<float>(MAP_FINE / width));
		// the samples between, along the two straight halves
		for (int half = 0; half < 2; ++half) {
			const int from = half == 0 ? i0 : mid, to = half == 0 ? mid : i1;
			for (int i = from + 1; i < to; ++i) {
				const double f = static_cast<double>(i - from) / (to - from);
				for (int k = 0; k < per; ++k) {
					const MapRay &a = ray(from, k), &b = ray(to, k);
					MapRay &out = ray(i, k);
					out.landing = a.landing + (b.landing - a.landing) * f;
					out.pass = a.pass + (b.pass - a.pass) * f;
					out.chord = std::sqrt(a.chord * a.chord + (b.chord * b.chord - a.chord * a.chord) * f);
					out.route = a.route;
				}
				known[static_cast<size_t>(i)] = 1;
			}
		}
	};
	for (int c = 0; c < map.coarse; ++c) {
		refine(refine, c * MAP_FINE, (c + 1) * MAP_FINE, c);
	}

	// how fast each whole pixel's landing bends in wavelength, at most: the second difference between rows,
	// over the map's slope across (as a shift on the screen), in pixels per nm^2. A band's sweep is taken in
	// as many straight steps as keep it true (the renderer: map_steps).
	map.bend.assign(static_cast<size_t>(map.coarse), 0.0f);
	const double row_nm = 400.0 / (map.rows - 1);
	for (int c = 0; c < map.coarse; ++c) {
		double most = 0.0;
		for (int i = c * MAP_FINE; i < (c + 1) * MAP_FINE; ++i) {
			for (int medium = 0; medium < map.media; ++medium) {
				for (int row = 1; row + 1 < map.rows; ++row) {
					const int k = medium * map.rows + row;
					const MapRay &before = ray(i, k - 1), &here = ray(i, k), &after = ray(i, k + 1), &beside = ray(i + 1, k);
					if (before.route != here.route || after.route != here.route || beside.route != here.route || here.pass < 0.02) {
						continue;
					}
					const double slope = std::max(1.0, std::fabs(beside.landing - here.landing) * MAP_FINE);
					most = std::max(most, std::fabs(before.landing - 2.0 * here.landing + after.landing) / (row_nm * row_nm) / slope);
				}
			}
		}
		map.bend[static_cast<size_t>(c)] = static_cast<float>(most);
	}

	// Routes as small whole numbers, in the order met. And where the route changes between a sample and the
	// next, how far along (found by halving, twelve times): in the route's fraction, half of it, so that a part
	// of a pixel across the change is shared out truly (a rim: none of it to the ray that misses).
	std::map<uint32_t, int> ids;
	map.cells.resize(static_cast<size_t>(per) * static_cast<size_t>(n) * 4);
	for (int k = 0; k < per; ++k) {
		const int medium = k / map.rows;
		const double nm = 380.0 + 400.0 * (k % map.rows) / (map.rows - 1);
		for (int i = 0; i < n; ++i) {
			const MapRay &r = ray(i, k);
			const auto found = ids.emplace(r.route, static_cast<int>(ids.size())).first;
			double along_cell = 0.0;
			if (i + 1 < n && ray(i + 1, k).route != r.route) {
				double lo = 0.0, hi = 1.0;
				for (int it = 0; it < 12; ++it) {
					const double mid = 0.5 * (lo + hi);
					(map_ray(spec, map.left + (i + mid) / MAP_FINE, nm, medium).route == r.route ? lo : hi) = mid;
					++map.rays;
				}
				along_cell = 0.5 * (lo + hi);
			}
			float *cell = &map.cells[((static_cast<size_t>(medium) * static_cast<size_t>(n) + static_cast<size_t>(i)) * static_cast<size_t>(map.rows) + static_cast<size_t>(k % map.rows)) * 4];
			cell[0] = static_cast<float>(r.landing);
			cell[1] = static_cast<float>(r.pass);
			cell[2] = static_cast<float>(r.chord * r.chord); // its square: straight across, where the chord rises as a root at a rim
			cell[3] = static_cast<float>(found->second + 0.5 * along_cell);
		}
	}
	return map;
}

MapRay map_read(const Map &map, double x, double nm, int medium) {
	const int n = map.samples();
	const double s = std::min(std::max((x - map.left) * MAP_FINE, 0.0), static_cast<double>(n - 1));
	const int i = std::min(static_cast<int>(s), n - 2);
	double f = s - i;
	const double r = std::min(std::max((nm - 380.0) / 400.0, 0.0), 1.0) * (map.rows - 1);
	const int k = std::min(static_cast<int>(r), map.rows - 2);
	const double g = r - k;
	const float *a0 = map.cell(medium, k, i), *a1 = map.cell(medium, k, i + 1), *c0 = map.cell(medium, k + 1, i), *c1 = map.cell(medium, k + 1, i + 1);
	const auto whole = [](float route) { return std::floor(route); };
	if (whole(a0[3]) != whole(a1[3]) || whole(c0[3]) != whole(c1[3])) {
		// the route changes between the samples: each side of the change its own sample's ray
		const double change = 2.0 * (whole(a0[3]) != whole(a1[3]) ? a0[3] - whole(a0[3]) : c0[3] - whole(c0[3]));
		f = f < change ? 0.0 : 1.0;
	}
	MapRay out;
	const auto across = [&](const float *lo, const float *hi, int part) { return lo[part] * (1.0 - f) + hi[part] * f; };
	out.pass = across(a0, a1, 1) * (1.0 - g) + across(c0, c1, 1) * g;
	out.chord = std::sqrt(std::max(across(a0, a1, 2) * (1.0 - g) + across(c0, c1, 2) * g, 0.0));
	const float route = whole(f < 0.5 ? a0[3] : a1[3]);
	out.route = static_cast<uint32_t>(route);
	// the landing: the cubic through four rows (Catmull-Rom), where they are all this route; a row that is
	// not, or is past the table's end, is the straight line's continuation
	const double p1 = across(a0, a1, 0), p2 = across(c0, c1, 0);
	double p0 = 2.0 * p1 - p2, p3 = 2.0 * p2 - p1;
	if (k > 0) {
		const float *b0 = map.cell(medium, k - 1, i), *b1 = map.cell(medium, k - 1, i + 1);
		if (whole(f < 0.5 ? b0[3] : b1[3]) == route && whole(b0[3]) == whole(b1[3])) {
			p0 = across(b0, b1, 0);
		}
	}
	if (k + 2 < map.rows) {
		const float *d0 = map.cell(medium, k + 2, i), *d1 = map.cell(medium, k + 2, i + 1);
		if (whole(f < 0.5 ? d0[3] : d1[3]) == route && whole(d0[3]) == whole(d1[3])) {
			p3 = across(d0, d1, 0);
		}
	}
	const bool one_route = whole(f < 0.5 ? c0[3] : c1[3]) == route;
	out.landing = one_route ? p1 + 0.5 * g * (p2 - p0 + g * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + g * (3.0 * (p1 - p2) + p3 - p0))) : (g < 0.5 ? p1 : p2);
	return out;
}

std::vector<float> map_block(const Map &map) {
	std::vector<float> out(static_cast<size_t>(1 + map.coarse) * 4 + map.cells.size(), 0.0f);
	out[0] = static_cast<float>(map.coarse);
	out[1] = static_cast<float>(map.media);
	out[2] = static_cast<float>(map.samples());
	out[3] = static_cast<float>(map.rows);
	for (int c = 0; c < map.coarse; ++c) {
		out[static_cast<size_t>(1 + c) * 4] = map.level[static_cast<size_t>(c)];
		out[static_cast<size_t>(1 + c) * 4 + 1] = map.bend[static_cast<size_t>(c)];
	}
	std::copy(map.cells.begin(), map.cells.end(), out.begin() + static_cast<std::ptrdiff_t>(1 + map.coarse) * 4);
	return out;
}

} // namespace lab::trace
