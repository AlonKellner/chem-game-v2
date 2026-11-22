// A vessel in its whole shape (GAME_SPEC §12.3, Stage 24): profiles of straight pieces and arcs.
#include "lab/glassware.hpp"

#include "lab/glassware_data.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <numbers>

namespace lab::glass {

namespace {

using std::numbers::pi;

Point add(Point a, Point b) { return { a.r + b.r, a.y + b.y }; }
Point sub(Point a, Point b) { return { a.r - b.r, a.y - b.y }; }
Point mul(Point a, double k) { return { a.r * k, a.y * k }; }
double dot(Point a, Point b) { return a.r * b.r + a.y * b.y; }
double cross(Point a, Point b) { return a.r * b.y - a.y * b.r; }
double length(Point a) { return std::sqrt(dot(a, a)); }
Point unit(Point a) { return mul(a, 1.0 / length(a)); }
Point left(Point d) { return { -d.y, d.r }; } // the normal to the left of a direction: into the glass

// where two lines (a point and a direction each) meet
Point meet(Point p, Point d, Point q, Point e) {
	const double den = cross(d, e);
	if (std::abs(den) < 1e-15) {
		return q;
	}
	return add(p, mul(d, cross(sub(q, p), e) / den));
}

const nlohmann::json &table() {
	static const nlohmann::json t = nlohmann::json::parse(GLASSWARE_JSON);
	return t;
}

// the piece's height at s, and a piece's s where its height is y (it runs up or down, never both)
double s_at(const Piece &p, double y) {
	double lo = 0.0, hi = 1.0;
	const bool up = p.at(1.0).y > p.at(0.0).y;
	for (int i = 0; i < 80; ++i) {
		const double mid = 0.5 * (lo + hi);
		((p.at(mid).y < y) == up ? lo : hi) = mid;
	}
	return 0.5 * (lo + hi);
}

// pi r^2 dy along a piece, from s = 0 to s1
double swept(const Piece &p, double s1) {
	if (!p.arc) {
		const Point b = p.at(s1);
		return pi / 3.0 * (b.y - p.a.y) * (p.a.r * p.a.r + p.a.r * b.r + b.r * b.r);
	}
	// Gauss-Legendre, 20 points: the integrand is smooth in the arc's angle
	static const double x[10] = { 0.0765265211334973, 0.2277858511416451, 0.3737060887154195, 0.5108670019508271, 0.6360536807265150, 0.7463319064601508,
		0.8391169718222188, 0.9122344282513259, 0.9639719272779138, 0.9931285991850949 };
	static const double w[10] = { 0.1527533871307258, 0.1491729864726037, 0.1420961093183820, 0.1316886384491766, 0.1181945319615184, 0.1019301198172404,
		0.0832767415767048, 0.0626720483341091, 0.0406014298003869, 0.0176140071391521 };
	double sum = 0.0;
	const double dt = (p.t1 - p.t0) * s1;
	for (int i = 0; i < 20; ++i) {
		const double u = 0.5 * s1 * (1.0 + (i < 10 ? -x[i] : x[i - 10]));
		const double t = p.t0 + (p.t1 - p.t0) * u;
		const double r = p.centre.r + p.radius * std::cos(t);
		sum += w[i % 10] * r * r * p.radius * std::cos(t);
	}
	return pi * sum * 0.5 * dt;
}

double radius_on(const Chain &chain, double y) {
	double r = 0.0;
	bool any = false;
	for (const Piece &p : chain) {
		const double y0 = std::min(p.a.y, p.b.y), y1 = std::max(p.a.y, p.b.y);
		if (y < y0 - 1e-12 || y > y1 + 1e-12) {
			continue;
		}
		any = true;
		if (y1 - y0 < 1e-15) {
			r = std::max(r, std::max(p.a.r, p.b.r));
		} else {
			r = std::max(r, p.at(s_at(p, y)).r);
		}
	}
	return any ? r : 0.0;
}

// the vertex where a widest-at-R fillet of radius `fillet` turns a flat base up a line to `toward`
double base_corner(double R, double fillet, Point toward) {
	double v = R;
	for (int i = 0; i < 60; ++i) {
		const Point d2 = unit(sub(toward, Point{ v, 0.0 }));
		const double turn = std::acos(std::clamp(d2.r, -1.0, 1.0));
		v = R - fillet + fillet * std::tan(0.5 * turn);
	}
	return v;
}

// the polyline's last segment carried on to height y
void reach(std::vector<Corner> &corners, double y) {
	const size_t n = corners.size();
	const Point d = sub(corners[n - 1].p, corners[n - 2].p);
	if (std::abs(d.y) > 1e-15) {
		corners[n - 1].p = add(corners[n - 2].p, mul(d, (y - corners[n - 2].p.y) / d.y));
	}
}

} // namespace

Point Piece::at(double s) const {
	if (!arc) {
		return add(a, mul(sub(b, a), s));
	}
	const double t = t0 + (t1 - t0) * s;
	return { centre.r + radius * std::cos(t), centre.y + radius * std::sin(t) };
}

Chain rounded(const std::vector<Corner> &corners) {
	const size_t n = corners.size();
	Chain chain;
	if (n < 2) {
		return chain;
	}
	// how far each corner's arc reaches along its two segments, scaled down where two would overlap
	std::vector<double> reach_of(n, 0.0), turn(n, 0.0), side(n, 0.0);
	for (size_t k = 1; k + 1 < n; ++k) {
		const Point d1 = unit(sub(corners[k].p, corners[k - 1].p)), d2 = unit(sub(corners[k + 1].p, corners[k].p));
		turn[k] = std::acos(std::clamp(dot(d1, d2), -1.0, 1.0));
		side[k] = cross(d1, d2) >= 0.0 ? 1.0 : -1.0;
		if (corners[k].fillet > 0.0 && turn[k] > 1e-12) {
			reach_of[k] = corners[k].fillet * std::tan(0.5 * turn[k]);
		}
	}
	for (size_t i = 0; i + 1 < n; ++i) {
		const double len = length(sub(corners[i + 1].p, corners[i].p));
		const double want = reach_of[i] + reach_of[i + 1];
		if (want > len) {
			reach_of[i] *= len / want;
			reach_of[i + 1] *= len / want;
		}
	}
	Point from = corners[0].p;
	for (size_t k = 1; k < n; ++k) {
		const Point d1 = unit(sub(corners[k].p, corners[k - 1].p));
		const Point to = sub(corners[k].p, mul(d1, reach_of[k]));
		if (length(sub(to, from)) > 1e-12) {
			Piece line;
			line.a = from;
			line.b = to;
			chain.push_back(line);
		}
		from = to;
		if (k + 1 < n && reach_of[k] > 0.0) {
			const Point d2 = unit(sub(corners[k + 1].p, corners[k].p));
			const double radius = reach_of[k] / std::tan(0.5 * turn[k]);
			Piece arc;
			arc.arc = true;
			arc.a = to;
			arc.b = add(corners[k].p, mul(d2, reach_of[k]));
			arc.centre = add(to, mul(left(d1), side[k] * radius));
			arc.radius = radius;
			arc.t0 = std::atan2(arc.a.y - arc.centre.y, arc.a.r - arc.centre.r);
			arc.t1 = std::atan2(arc.b.y - arc.centre.y, arc.b.r - arc.centre.r);
			while (side[k] > 0.0 && arc.t1 < arc.t0) {
				arc.t1 += 2.0 * pi;
			}
			while (side[k] < 0.0 && arc.t1 > arc.t0) {
				arc.t1 -= 2.0 * pi;
			}
			chain.push_back(arc);
			from = arc.b;
		}
	}
	return chain;
}

std::vector<Corner> inward(const std::vector<Corner> &corners) {
	const size_t n = corners.size();
	std::vector<Corner> out = corners;
	if (n < 2) {
		return out;
	}
	std::vector<Point> p(n - 1), d(n - 1);
	for (size_t i = 0; i + 1 < n; ++i) {
		d[i] = unit(sub(corners[i + 1].p, corners[i].p));
		p[i] = add(corners[i].p, mul(left(d[i]), corners[i].thickness));
	}
	// the first corner on the axis, the last where its segment ends, moved; each between where two meet
	out[0].p = std::abs(d[0].r) > 1e-15 ? add(p[0], mul(d[0], -p[0].r / d[0].r)) : p[0];
	out[n - 1].p = add(corners[n - 1].p, mul(left(d[n - 2]), corners[n - 2].thickness));
	for (size_t k = 1; k + 1 < n; ++k) {
		out[k].p = meet(p[k - 1], d[k - 1], p[k], d[k]);
		const double moved = 0.5 * (corners[k - 1].thickness + corners[k].thickness);
		out[k].fillet = cross(d[k - 1], d[k]) >= 0.0 ? std::max(0.0, corners[k].fillet - moved) : corners[k].fillet + moved;
	}
	return out;
}

Chain below(const Chain &chain, double y) {
	Chain out;
	for (const Piece &p : chain) {
		if (std::max(p.a.y, p.b.y) <= y + 1e-12) {
			out.push_back(p);
			continue;
		}
		if (std::min(p.a.y, p.b.y) < y) {
			const double s = s_at(p, y);
			Piece cut = p;
			cut.b = p.at(s);
			cut.b.y = y;
			if (p.arc) {
				cut.t1 = p.t0 + (p.t1 - p.t0) * s;
			}
			out.push_back(cut);
		}
		break;
	}
	return out;
}

double Vessel::volume_below(double y) const {
	double v = 0.0;
	for (const Piece &p : inside) {
		const double y0 = std::min(p.a.y, p.b.y), y1 = std::max(p.a.y, p.b.y);
		if (y1 - y0 < 1e-15 || y0 >= y) {
			continue;
		}
		v += swept(p, y1 <= y ? 1.0 : s_at(p, y));
	}
	return v;
}

double Vessel::capacity() const {
	return volume_below(height);
}

double Vessel::level(double cm3) const {
	if (cm3 <= 0.0) {
		return floor;
	}
	if (cm3 >= capacity()) {
		return height;
	}
	double lo = floor, hi = height;
	for (int i = 0; i < 70; ++i) {
		const double mid = 0.5 * (lo + hi);
		(volume_below(mid) < cm3 ? lo : hi) = mid;
	}
	return 0.5 * (lo + hi);
}

double Vessel::mouth() const {
	return radius_inside(height - 1e-6);
}

double Vessel::head_inverted(double cm3) const {
	// upside down, what it holds lies from its mouth: the depth over the mouth that holds cm3
	if (cm3 <= 0.0) {
		return 0.0;
	}
	const double all = capacity();
	if (cm3 >= all) {
		return height - floor;
	}
	double lo = 0.0, hi = height - floor;
	for (int i = 0; i < 70; ++i) {
		const double mid = 0.5 * (lo + hi);
		((all - volume_below(height - mid)) < cm3 ? lo : hi) = mid;
	}
	return 0.5 * (lo + hi);
}

double seals() {
	return table().at("seals_mm").get<double>() / 10.0;
}

double burst_default() {
	return table().at("burst_kpa").get<double>() * 1000.0;
}

double Vessel::radius_outside(double y) const {
	return radius_on(outside, y);
}

double Vessel::radius_inside(double y) const {
	return y < floor ? 0.0 : radius_on(inside, y);
}

double Vessel::widest() const {
	double r = 0.0;
	for (const Piece &p : outside) {
		for (int i = 0; i <= 64; ++i) {
			r = std::max(r, p.at(i / 64.0).r);
		}
		if (p.arc) {
			// the circle's own extreme, if the arc reaches it
			const double lo = std::min(p.t0, p.t1), hi = std::max(p.t0, p.t1);
			for (int k = -2; k <= 2; ++k) {
				if (k * 2.0 * pi >= lo && k * 2.0 * pi <= hi) {
					r = std::max(r, p.centre.r + p.radius);
				}
			}
		}
	}
	return r;
}

const Vessel *find(std::string_view kind, double nominal_ml) {
	static std::mutex lock;
	static std::vector<std::unique_ptr<Vessel>> made;
	const std::lock_guard<std::mutex> hold(lock);
	for (const auto &v : made) {
		if (v->kind == kind && v->nominal_ml == nominal_ml) {
			return v.get();
		}
	}
	std::string why;
	std::optional<Vessel> v = make(kind, nominal_ml, why);
	if (!v) {
		return nullptr;
	}
	made.push_back(std::make_unique<Vessel>(std::move(*v)));
	return made.back().get();
}

std::optional<Vessel> make(std::string_view kind, double nominal_ml, std::string &why) {
	const nlohmann::json *k = nullptr;
	for (const nlohmann::json &each : table().at("kinds")) {
		if (each.at("name").get<std::string>() == kind) {
			k = &each;
		}
	}
	if (!k) {
		why = "no glassware of kind '" + std::string(kind) + "'";
		return std::nullopt;
	}
	const nlohmann::json *row = nullptr;
	for (const nlohmann::json &size : k->at("sizes")) {
		if (size.at("ml").get<double>() == nominal_ml) {
			row = &size;
		}
	}
	if (!row) {
		why = "no " + std::string(kind) + " of " + std::to_string(nominal_ml) + " mL in its standard's table";
		return std::nullopt;
	}
	const auto mm = [&](const char *key) { return row->at(key).get<double>() / 10.0; };
	Vessel v;
	v.kind = kind;
	v.nominal_ml = nominal_ml;
	v.diameter = mm("diameter_mm");
	v.height = mm("height_mm");
	v.wall = mm("wall_mm");
	v.floor = v.wall * k->at("floor_per_wall").get<double>();
	v.pour_ml_s = k->at("pour_ml_s").get<double>();
	v.cap_bore = k->at("cap_bore_mm").get<double>() / 10.0;
	v.burst = k->at("burst_kpa").get<double>() * 1000.0;
	v.screw_cap = k->value("screw_cap", false);
	const double R = 0.5 * v.diameter, H = v.height, t = v.wall, f = v.floor;
	std::vector<Corner> c;
	std::vector<Piece> top; // what follows the polyline outside: a neck's bead
	if (kind == "beaker") {
		// ISO 3819 §7.2: a brim wider than the body, flared in one arc tangent to the side, ending at the brim
		const double widening = 0.5 * k->at("brim").get<double>() * v.diameter;
		const double rise = k->at("flare_rise").get<double>() * widening;
		const double under = (rise * rise - widening * widening) / (2.0 * rise); // the polyline's corner under the brim
		const double run = std::sqrt(widening * widening + under * under);
		const double turn = std::atan2(widening, under);
		c = { { { 0.0, 0.0 }, 0.0, f }, { { R, 0.0 }, row->at("corner").get<double>() * v.diameter, t }, { { R, H - under }, run / std::tan(0.5 * turn), t },
			{ { R + widening, H }, 0.0, t } };
	} else if (kind == "conical_flask") {
		// ISO 1773: widest at the base's corner, a cone to the neck, the neck with a bead on its top
		const double rn = mm("neck_mm") / 2.0, corner = k->at("corner").get<double>() * v.diameter;
		const double shoulder = H - k->at("neck_length").get<double>() * 2.0 * rn;
		const double vx = base_corner(R, corner, { rn, shoulder });
		c = { { { 0.0, 0.0 }, 0.0, f }, { { vx, 0.0 }, corner, t }, { { rn, shoulder }, k->at("shoulder").get<double>() * 2.0 * rn, t },
			{ { rn, H - 2.0 * t }, 0.0, t } };
		Piece bead;
		bead.arc = true;
		bead.centre = { rn, H - t };
		bead.radius = t;
		bead.t0 = -0.5 * pi;
		bead.t1 = 0.5 * pi;
		bead.a = bead.at(0.0);
		bead.b = bead.at(1.0);
		top.push_back(bead);
	} else if (kind == "bottle") {
		// ISO 4796-1 §5.2: a radius at the base and at the shoulder, a cone to the neck, a strengthening lip
		const double rn = mm("neck_mm") / 2.0, rin = mm("neck_inside_mm") / 2.0, neck = rn - rin;
		const double shoulder = mm("shoulder_mm"), base_of_neck = H - mm("neck_height_mm");
		c = { { { 0.0, 0.0 }, 0.0, f }, { { R, 0.0 }, k->at("corner").get<double>() * v.diameter, t },
			{ { R, shoulder }, k->at("shoulder").get<double>() * v.diameter, t }, { { rn, base_of_neck }, k->at("neck_radius_mm").get<double>() / 10.0, neck },
			{ { rn, H - 2.0 * t }, 0.0, neck } };
		Piece bead;
		bead.arc = true;
		bead.centre = { rn, H - t };
		bead.radius = t;
		bead.t0 = -0.5 * pi;
		bead.t1 = 0.5 * pi;
		bead.a = bead.at(0.0);
		bead.b = bead.at(1.0);
		top.push_back(bead);
	} else {
		why = "no profile for glassware of kind '" + std::string(kind) + "'";
		return std::nullopt;
	}
	v.outside = rounded(c);
	for (const Piece &p : top) {
		v.outside.push_back(p);
	}
	std::vector<Corner> in = inward(c);
	reach(in, H);
	v.inside = below(rounded(in), H);
	return v;
}

std::vector<Offer> offered() {
	std::vector<Offer> out;
	for (const nlohmann::json &k : table().at("kinds")) {
		Offer o;
		o.kind = k.at("name").get<std::string>();
		o.standard = k.at("standard").get<std::string>();
		for (const nlohmann::json &size : k.at("sizes")) {
			o.ml.push_back(size.at("ml").get<double>());
		}
		out.push_back(o);
	}
	return out;
}

} // namespace lab::glass
