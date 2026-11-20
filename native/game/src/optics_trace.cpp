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
	return spec.media >= 2 && medium == 0;
}

} // namespace

int Map::samples() const {
	return coarse * MAP_FINE + 1;
}

MapRay map_ray(const MapSpec &spec, double x, double nm, int medium) {
	// its core: its liquid, the air over it, or (a third core: a vessel's floor) glass all through
	const double glass = index_at(spec.glass_index, nm);
	std::array<double, MAX_MEDIA> index{ 1.0, glass, liquid(spec, medium) ? index_at(spec.liquid_index, nm) : medium == 2 ? glass : 1.0, 1.0 };
	const Landing l = trace(spec.shape, Vec3{ x * spec.cm_per_pixel, 0.0, -1000.0 }, Vec3{ 0.0, 0.0, 1.0 }, spec.wall_cm, index);
	MapRay out;
	out.landing = l.lands ? l.x / spec.cm_per_pixel : l.x;
	out.pass = l.pass;
	out.chord = liquid(spec, medium) ? l.chord[CORE] : 0.0;
	out.route = l.route;
	return out;
}

double map_place(double nm) {
	const double r = 380.0 / std::min(std::max(nm, 380.0), 780.0);
	return std::min(std::max((1.0 - r * r) / MAP_PLACE_SPAN, 0.0), 1.0);
}

double map_nm(double place) {
	return 380.0 / std::sqrt(1.0 - MAP_PLACE_SPAN * std::min(std::max(place, 0.0), 1.0));
}

namespace {

// the cubic through four values at the thirds of a piece, at g of the way through it
double thirds(double v0, double v1, double v2, double v3, double g) {
	const double s = 3.0 * g;
	return -(s - 1.0) * (s - 2.0) * (s - 3.0) / 6.0 * v0 + s * (s - 2.0) * (s - 3.0) / 2.0 * v1 - s * (s - 1.0) * (s - 3.0) / 2.0 * v2 + s * (s - 1.0) * (s - 2.0) / 6.0 * v3;
}

} // namespace

Map build_map(const MapSpec &spec) {
	Map map;
	map.media = spec.media;
	map.left = std::floor(spec.left - spec.grid) + spec.grid;
	map.coarse = std::max(1, static_cast<int>(std::ceil(spec.right - map.left)));
	const int n = map.samples();

	// The pieces: the fewest, even in 1 / nm^2, in each of which the cubic through a ray's four nodes is the
	// ray: tried on rays across the body, in the middle of each third of each piece. A landing may be off by
	// MAP_LANDING_TOLERANCE as a shift of the image on the screen, or by MAP_NM_TOLERANCE as a shift in
	// wavelength (where a ray leaves a face nearly along it, its landing runs away with the wavelength, and no
	// cubic follows it: but the light there is spread as far, and a nanometre's twentieth of it is nothing).
	map.pieces = 1;
	{
		constexpr int PROBES = 33;
		for (int p = 0; p < PROBES; ++p) {
			const double x = spec.left + (spec.right - spec.left) * (p + 0.5) / PROBES;
			for (int medium = 0; medium < spec.media; ++medium) {
				// how fast the landing moves across the body here: an error on the wall shifts the image by
				// that much less on the screen
				const MapRay here = map_ray(spec, x, 580.0, medium), beside = map_ray(spec, x + 1.0 / MAP_FINE, 580.0, medium);
				map.rays += 2;
				const double slope = std::max(1.0, std::fabs(beside.landing - here.landing) * MAP_FINE);
				for (; map.pieces < MAP_PIECES_MOST;) {
					bool true_to_it = true;
					for (int piece = 0; piece < map.pieces && true_to_it; ++piece) {
						MapRay node[4];
						double nm[4];
						for (int k = 0; k < 4; ++k) {
							nm[k] = map_nm((piece + k / 3.0) / map.pieces);
							node[k] = map_ray(spec, x, nm[k], medium);
							++map.rays;
						}
						for (int third = 0; third < 3 && true_to_it; ++third) {
							const double g = (third + 0.5) / 3.0;
							const MapRay exact = map_ray(spec, x, map_nm((piece + g) / map.pieces), medium);
							++map.rays;
							if (exact.route != node[0].route || node[1].route != node[0].route || node[2].route != node[0].route || node[3].route != node[0].route || exact.pass < 0.02) {
								continue; // a ray that changes its way in the piece is read at the piece's nearer end
							}
							const double per_nm = std::fabs(node[third + 1].landing - node[third].landing) / std::max(nm[third + 1] - nm[third], 1e-9);
							const auto sq = [](const MapRay &r) { return r.chord * r.chord; };
							true_to_it = std::fabs(thirds(node[0].landing, node[1].landing, node[2].landing, node[3].landing, g) - exact.landing) <=
												 std::max(MAP_LANDING_TOLERANCE * slope, MAP_NM_TOLERANCE * per_nm) &&
										 std::fabs(thirds(node[0].pass, node[1].pass, node[2].pass, node[3].pass, g) - exact.pass) <= MAP_PASS_TOLERANCE &&
										 std::fabs(thirds(sq(node[0]), sq(node[1]), sq(node[2]), sq(node[3]), g) - sq(exact)) <= 0.01 * std::max(1.0, sq(exact));
						}
					}
					if (true_to_it) {
						break;
					}
					map.pieces *= 2;
				}
			}
		}
	}

	for (;;) {
	// the cells: traced where the map bends across the body, filled along the straight line where it does not
	const int nodes = 3 * map.pieces + 1, per = map.media * nodes; // rays a sample
	std::vector<double> node_nm(static_cast<size_t>(nodes));
	for (int j = 0; j < nodes; ++j) {
		node_nm[static_cast<size_t>(j)] = map_nm(static_cast<double>(j) / (nodes - 1));
	}
	std::vector<MapRay> rays(static_cast<size_t>(n) * static_cast<size_t>(per));
	std::vector<char> known(static_cast<size_t>(n), 0);
	const auto shoot = [&](int i) {
		if (known[static_cast<size_t>(i)]) {
			return;
		}
		for (int medium = 0; medium < map.media; ++medium) {
			for (int j = 0; j < nodes; ++j) {
				rays[static_cast<size_t>(i) * static_cast<size_t>(per) + static_cast<size_t>(medium * nodes + j)] =
						map_ray(spec, map.left + static_cast<double>(i) / MAP_FINE, node_nm[static_cast<size_t>(j)], medium);
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

	// how fast each whole pixel's landing bends in wavelength, at most: the second difference between a
	// ray's nodes, over the map's slope across (as a shift on the screen), in pixels per nm^2. A band's sweep
	// is taken in as many straight steps as keep it true (the renderer: map_steps).
	map.bend.assign(static_cast<size_t>(map.coarse), 0.0f);
	for (int c = 0; c < map.coarse; ++c) {
		double most = 0.0;
		for (int i = c * MAP_FINE; i < (c + 1) * MAP_FINE; ++i) {
			for (int medium = 0; medium < map.media; ++medium) {
				for (int j = 1; j + 1 < nodes; ++j) {
					const int k = medium * nodes + j;
					const MapRay &before = ray(i, k - 1), &here = ray(i, k), &after = ray(i, k + 1), &beside = ray(i + 1, k);
					if (before.route != here.route || after.route != here.route || beside.route != here.route || here.pass < 0.02) {
						continue;
					}
					const double slope = std::max(1.0, std::fabs(beside.landing - here.landing) * MAP_FINE);
					const double nm0 = node_nm[static_cast<size_t>(j - 1)], nm1 = node_nm[static_cast<size_t>(j)], nm2 = node_nm[static_cast<size_t>(j + 1)];
					const double second = 2.0 * ((after.landing - here.landing) / (nm2 - nm1) - (here.landing - before.landing) / (nm1 - nm0)) / (nm2 - nm0);
					most = std::max(most, std::fabs(second) / slope);
				}
			}
		}
		map.bend[static_cast<size_t>(c)] = static_cast<float>(most);
	}

	// Routes as small whole numbers, in the order met. And where the route changes between a sample and the
	// next, how far along (found by halving, twelve times): in the route's fraction, half of it, so that a part
	// of a pixel across the change is shared out truly (a rim: none of it to the ray that misses).
	std::map<uint32_t, int> ids;
	std::vector<float> routes(static_cast<size_t>(n) * static_cast<size_t>(per));
	// Where a route's boundary across the body moves with the wavelength, a ray near it changes its way within
	// a piece, and is read there as its nearest node's ray: wrongly over at most half the way the boundary
	// moves between two nodes. That is kept to MAP_LANDING_TOLERANCE of the screen, by more pieces.
	double moved = 0.0;
	std::vector<std::array<double, 3>> before, here; // a node's boundaries: where, and the routes on its two sides
	for (int medium = 0; medium < map.media; ++medium) {
		before.clear();
		for (int j = 0; j < nodes; ++j) {
			const int k = medium * nodes + j;
			here.clear();
			for (int i = 0; i < n; ++i) {
				const MapRay &r = ray(i, k);
				const auto found = ids.emplace(r.route, static_cast<int>(ids.size())).first;
				double along_cell = 0.0;
				if (i + 1 < n && ray(i + 1, k).route != r.route) {
					double lo = 0.0, hi = 1.0;
					for (int it = 0; it < 12; ++it) {
						const double mid = 0.5 * (lo + hi);
						(map_ray(spec, map.left + (i + mid) / MAP_FINE, node_nm[static_cast<size_t>(j)], medium).route == r.route ? lo : hi) = mid;
						++map.rays;
					}
					along_cell = 0.5 * (lo + hi);
					here.push_back({ (i + along_cell) / MAP_FINE, static_cast<double>(r.route), static_cast<double>(ray(i + 1, k).route) });
				}
				routes[static_cast<size_t>(i) * static_cast<size_t>(per) + static_cast<size_t>(k)] = static_cast<float>(found->second + 0.5 * along_cell);
			}
			if (j > 0) {
				for (const auto &now : here) {
					double nearest = -1.0;
					for (const auto &was : before) {
						if (was[1] == now[1] && was[2] == now[2] && (nearest < 0.0 || std::fabs(was[0] - now[0]) < nearest)) {
							nearest = std::fabs(was[0] - now[0]);
						}
					}
					moved = std::max(moved, nearest); // one that was not there before: not counted
				}
			}
			before = here;
		}
	}
	int wanted = map.pieces;
	while (wanted < MAP_PIECES_MOST && 0.5 * moved * map.pieces / wanted > MAP_LANDING_TOLERANCE) {
		wanted *= 2;
	}
	if (wanted > map.pieces) {
		map.pieces = wanted;
		continue; // again, finer
	}
	map.cells.resize(static_cast<size_t>(map.media) * static_cast<size_t>(n) * static_cast<size_t>(map.pieces) * MAP_CELL);
	for (int medium = 0; medium < map.media; ++medium) {
		for (int i = 0; i < n; ++i) {
			for (int piece = 0; piece < map.pieces; ++piece) {
				float *cell = &map.cells[((static_cast<size_t>(medium) * static_cast<size_t>(n) + static_cast<size_t>(i)) * static_cast<size_t>(map.pieces) + static_cast<size_t>(piece)) * MAP_CELL];
				for (int k = 0; k < 4; ++k) {
					const MapRay &r = ray(i, medium * nodes + 3 * piece + k);
					cell[k] = static_cast<float>(r.landing);
					cell[4 + k] = static_cast<float>(r.pass);
					cell[8 + k] = static_cast<float>(r.chord * r.chord); // its square: straight across, where the chord rises as a root at a rim
					cell[12 + k] = routes[static_cast<size_t>(i) * static_cast<size_t>(per) + static_cast<size_t>(medium * nodes + 3 * piece + k)];
				}
			}
		}
	}
	return map;
	}
}

MapRay map_read(const Map &map, double x, double nm, int medium) {
	const int n = map.samples();
	const double s = std::min(std::max((x - map.left) * MAP_FINE, 0.0), static_cast<double>(n - 1));
	const int i = std::min(static_cast<int>(s), n - 2);
	double f = s - i;
	const double place = map_place(nm) * map.pieces;
	const int piece = std::min(static_cast<int>(place), map.pieces - 1);
	const double g = place - piece;
	const float *a = map.cell(medium, piece, i), *b = map.cell(medium, piece, i + 1);
	const auto whole = [](float route) { return std::floor(route); };
	for (int k = 0; k < 4; ++k) {
		if (whole(a[12 + k]) != whole(b[12 + k])) {
			// the route changes between the samples: each side of the change its own sample's ray
			f = f < 2.0 * (a[12 + k] - whole(a[12 + k])) ? 0.0 : 1.0;
			break;
		}
	}
	const float *near = f < 0.5 ? a : b;
	const auto one_way = [&](const float *c) { return whole(c[13]) == whole(c[12]) && whole(c[14]) == whole(c[12]) && whole(c[15]) == whole(c[12]); };
	const bool cubic = (f <= 0.0 || one_way(b)) && (f >= 1.0 || one_way(a));
	const int node = std::min(static_cast<int>(3.0 * g + 0.5), 3);
	const auto across = [&](int k) { return a[k] * (1.0 - f) + b[k] * f; };
	// the cubic through the piece's four nodes, where they all go one way; if not, the nearest node's ray
	const auto read = [&](int from) { return cubic ? thirds(across(from), across(from + 1), across(from + 2), across(from + 3), g) : across(from + node); };
	MapRay out;
	out.landing = read(0);
	out.pass = std::min(std::max(read(4), 0.0), 1.0);
	out.chord = std::sqrt(std::max(read(8), 0.0));
	out.route = static_cast<uint32_t>(whole(near[12 + (cubic ? 0 : node)]));
	return out;
}

std::vector<float> map_block(const Map &map) {
	std::vector<float> out(static_cast<size_t>(1 + map.coarse) * 4 + map.cells.size(), 0.0f);
	out[0] = static_cast<float>(map.coarse);
	out[1] = static_cast<float>(map.media);
	out[2] = static_cast<float>(map.samples());
	out[3] = static_cast<float>(map.pieces);
	for (int c = 0; c < map.coarse; ++c) {
		out[static_cast<size_t>(1 + c) * 4] = map.level[static_cast<size_t>(c)];
		out[static_cast<size_t>(1 + c) * 4 + 1] = map.bend[static_cast<size_t>(c)];
	}
	std::copy(map.cells.begin(), map.cells.end(), out.begin() + static_cast<std::ptrdiff_t>(1 + map.coarse) * 4);
	return out;
}

} // namespace lab::trace
