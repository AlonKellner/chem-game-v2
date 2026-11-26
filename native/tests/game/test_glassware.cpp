// A vessel in its whole shape (GAME_SPEC §12.3, Stage 24): glassware to its standard, one profile.
#include "lab/glassware.hpp"
#include "lab/glassware_data.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstring>
#include <numbers>
#include <string>
#include <tuple>

using namespace lab::glass;

namespace {

Vessel vessel(const char *kind, double ml) {
	std::string why;
	auto v = make(kind, ml, why);
	REQUIRE_MESSAGE(v.has_value(), why);
	return *v;
}

double distance(Point a, Point b) {
	return std::hypot(a.r - b.r, a.y - b.y);
}

// the direction a piece leaves its start, and arrives at its end
Point tangent(const Piece &p, double s) {
	const double h = 1e-7;
	const Point a = p.at(std::max(0.0, s - h)), b = p.at(std::min(1.0, s + h));
	const double l = distance(a, b);
	return { (b.r - a.r) / l, (b.y - a.y) / l };
}

// the inside's least distance from a point
double gap_to(const Chain &chain, Point p) {
	double best = 1e300;
	for (const Piece &piece : chain) {
		for (int i = 0; i <= 2000; ++i) {
			best = std::min(best, distance(piece.at(i / 2000.0), p));
		}
	}
	return best;
}

// the height where the inside first narrows to a neck of inner radius r (from above the body)
double neck_base(const Vessel &v, double r) {
	double y = v.height;
	while (y > v.floor && v.radius_inside(y) <= r + 1e-6) {
		y -= 1e-4;
	}
	return y;
}

} // namespace

TEST_CASE("glassware: every kind's rows are its standard's (Stage 24)") {
	// ISO 3819 Table: 50 mL 42 x 60 mm, 0.8 mm; 250 mL 70 x 95, 1.1 mm. ISO 1773 Table 1: 250 mL 85, neck 34,
	// 145 mm, 0.9 mm. ISO 4796-1 Table 1: 500 mL 86 across, 110 to the shoulder, 176 high, 1.5 mm
	const struct {
		const char *kind;
		double ml, diameter, height, wall;
	} rows[] = { { "beaker", 50, 4.2, 6.0, 0.08 }, { "beaker", 250, 7.0, 9.5, 0.11 }, { "conical_flask", 250, 8.5, 14.5, 0.09 }, { "bottle", 500, 8.6, 17.6, 0.15 } };
	for (const auto &row : rows) {
		CAPTURE(row.kind);
		CAPTURE(row.ml);
		const Vessel v = vessel(row.kind, row.ml);
		CHECK(v.diameter == doctest::Approx(row.diameter).epsilon(1e-12));
		CHECK(v.height == doctest::Approx(row.height).epsilon(1e-12));
		CHECK(v.wall == doctest::Approx(row.wall).epsilon(1e-12));
		CHECK(v.floor == doctest::Approx(2.36 * row.wall).epsilon(1e-12)); // calibrated: the supplier's floor to its side
		CHECK(v.outside.back().b.y == doctest::Approx(row.height).epsilon(1e-12));
		CHECK(v.inside.back().b.y == doctest::Approx(row.height).epsilon(1e-12));
	}
	// the body's diameter: a beaker's and a bottle's side, the flask's widest
	CHECK(vessel("beaker", 250).radius_outside(5.0) == doctest::Approx(3.5).epsilon(1e-12));
	CHECK(vessel("bottle", 500).radius_outside(6.0) == doctest::Approx(4.3).epsilon(1e-12));
	double widest = 0.0;
	const Vessel flask = vessel("conical_flask", 250);
	for (int i = 0; i <= 14500; ++i) {
		widest = std::max(widest, flask.radius_outside(i * 1e-3));
	}
	CHECK(widest == doctest::Approx(4.25).epsilon(1e-6));
	// ISO 3819 §7.2: the brim 5 to 15 % wider than the body (calibrated: 10 %)
	const Vessel beaker = vessel("beaker", 250);
	CHECK(beaker.outside.back().b.r / 3.5 == doctest::Approx(1.10).epsilon(1e-9));
	// necks: the flask's 34 mm outside (and its bead), the bottle's GL 45 and 33 mm inside (at least 27)
	CHECK(flask.radius_outside(14.0) == doctest::Approx(1.7).epsilon(1e-9));
	CHECK(flask.radius_outside(14.5 - 0.09) == doctest::Approx(1.79).epsilon(1e-9));
	const Vessel bottle = vessel("bottle", 500);
	CHECK(bottle.radius_outside(16.0) == doctest::Approx(2.25).epsilon(1e-9));
	CHECK(bottle.radius_inside(17.0) == doctest::Approx(1.65).epsilon(1e-9));
	CHECK(2.0 * bottle.radius_inside(17.0) >= 2.7);
	// no such row
	std::string why;
	CHECK_FALSE(make("beaker", 75, why).has_value());
	CHECK_FALSE(make("retort", 250, why).has_value());
}

TEST_CASE("glassware: a profile is one smooth line, and its wall as thick as its standard's least (Stage 24)") {
	for (const auto &[kind, ml] : { std::pair{ "beaker", 50.0 }, std::pair{ "beaker", 250.0 }, std::pair{ "conical_flask", 250.0 }, std::pair{ "bottle", 500.0 } }) {
		CAPTURE(kind);
		const Vessel v = vessel(kind, ml);
		for (const Chain *chain : { &v.outside, &v.inside }) {
			for (size_t i = 0; i + 1 < chain->size(); ++i) {
				CAPTURE(i);
				const Piece &a = (*chain)[i], &b = (*chain)[i + 1];
				CHECK(distance(a.b, b.a) < 1e-12);
				// along a common tangent, but where a neck's bead stands on it
				const bool bead = b.arc && std::abs(b.t1 - b.t0 - std::numbers::pi) < 1e-12;
				if (!bead) {
					const Point u = tangent(a, 1.0), w = tangent(b, 0.0);
					CHECK(u.r * w.r + u.y * w.y > 1.0 - 1e-6);
				}
			}
		}
		// the side's wall: the inside's least distance from the outside at half the body's height
		const double y = 0.4 * v.height;
		CHECK(gap_to(v.inside, { v.radius_outside(y), y }) == doctest::Approx(v.wall).epsilon(1e-4));
		// the floor
		CHECK(v.inside.front().a.y == doctest::Approx(v.floor).epsilon(1e-12));
		CHECK(v.inside.front().a.r == 0.0);
	}
}

TEST_CASE("glassware: volumes against closed forms: a cylinder, and a rounded corner by Pappus (Stage 24)") {
	const double R = 3.0, H = 10.0, rho = 1.2;
	Vessel cylinder;
	cylinder.height = H;
	cylinder.inside = rounded({ { { 0.0, 0.0 } }, { { R, 0.0 } }, { { R, H } } });
	CHECK(cylinder.capacity() == doctest::Approx(std::numbers::pi * R * R * H).epsilon(1e-12));
	CHECK(cylinder.volume_below(4.0) == doctest::Approx(std::numbers::pi * R * R * 4.0).epsilon(1e-12));
	// the corner rounded: the spandrel (a square less a quarter circle) turned about the axis. Its area
	// rho^2 (1 - pi/4), its centroid (10 - 3 pi) / (3 (4 - pi)) rho from the corner
	Vessel round = cylinder;
	round.inside = rounded({ { { 0.0, 0.0 } }, { { R, 0.0 }, rho }, { { R, H } } });
	const double area = rho * rho * (1.0 - std::numbers::pi / 4.0);
	const double centroid = R - (10.0 - 3.0 * std::numbers::pi) / (3.0 * (4.0 - std::numbers::pi)) * rho;
	CHECK(round.capacity() == doctest::Approx(std::numbers::pi * R * R * H - 2.0 * std::numbers::pi * centroid * area).epsilon(1e-12));
	// a cone (a frustum)
	Vessel cone = cylinder;
	cone.inside = rounded({ { { 0.0, 0.0 } }, { { R, 0.0 } }, { { 1.0, H } } });
	CHECK(cone.capacity() == doctest::Approx(std::numbers::pi * H / 3.0 * (R * R + R * 1.0 + 1.0)).epsilon(1e-12));
}

TEST_CASE("capacity: the standards' own volumes (Stage 24)") {
	// ISO 3819 §5: a beaker's overflow capacity exceeds its nominal by 10 %, or its nominal level is 10 mm
	// under the overflow, whichever is the greater
	for (const double ml : { 50.0, 250.0 }) {
		CAPTURE(ml);
		const Vessel v = vessel("beaker", ml);
		const double nominal_level = v.level(ml);
		CHECK(v.capacity() >= 1.10 * ml);
		CHECK(v.height - nominal_level >= 1.0);
		MESSAGE("beaker " << ml << " mL: " << v.capacity() << " mL to the brim, its nominal level " << 10.0 * (v.height - nominal_level) << " mm under it");
	}
	// ISO 1773 §3.2: the nominal capacity is the series value nearest to but not above the volume to the
	// neck's base (the series: 25, 50, 100, 250, 500)
	const Vessel flask = vessel("conical_flask", 250);
	const double to_neck = flask.volume_below(neck_base(flask, 1.7 - 0.09));
	CHECK(to_neck >= 250.0);
	CHECK(to_neck < 500.0);
	MESSAGE("conical flask: " << to_neck << " mL to the neck's base, " << flask.capacity() << " mL to the top");
	// ISO 4796-1 §3.2, §3.3: the nominal at the turn of the shoulder (a bottle of average wall), and about
	// 15 % more to the neck's base. The least wall holds more than an average one: within 10 % above
	// (the turn of the shoulder: where the inside leaves its cylinder)
	const Vessel bottle = vessel("bottle", 500);
	double turn = bottle.floor + 1.0;
	while (bottle.radius_inside(turn + 1e-4) >= 4.3 - 0.15 - 1e-9) {
		turn += 1e-4;
	}
	const double to_shoulder = bottle.volume_below(turn), to_bottle_neck = bottle.volume_below(neck_base(bottle, 1.65));
	CHECK(to_shoulder >= 500.0);
	CHECK(to_shoulder <= 550.0);
	CHECK(to_bottle_neck / to_shoulder == doctest::Approx(1.15).epsilon(0.03));
	MESSAGE("bottle: " << to_shoulder << " mL to the shoulder, " << to_bottle_neck << " to the neck's base, " << bottle.capacity() << " to the top");
}

TEST_CASE("glassware: the liquid's level is the inside's volume turned around (Stage 24)") {
	for (const auto &[kind, ml] : { std::pair{ "beaker", 250.0 }, std::pair{ "conical_flask", 250.0 }, std::pair{ "bottle", 500.0 } }) {
		CAPTURE(kind);
		const Vessel v = vessel(kind, ml);
		double last = v.floor;
		for (const double cm3 : { 0.0, 1.0, 50.0, 150.0, 240.0 }) {
			const double y = v.level(cm3);
			CHECK(y >= last);
			last = y;
			CHECK(v.volume_below(y) == doctest::Approx(cm3).epsilon(1e-9));
		}
		CHECK(v.level(1e9) == v.height);
	}
	// a cone fills faster as it narrows: the flask's second 100 mL rise higher than its first
	const Vessel flask = vessel("conical_flask", 250);
	CHECK(flask.level(200.0) - flask.level(100.0) > flask.level(100.0) - flask.level(0.0));
}

TEST_CASE("glassware: a mouth's path for a gas is its table's, the integral of dz / A(z) from what it holds to its brim (Stage 50)") {
	for (const lab::glass::Offer &o : lab::glass::offered()) {
		for (const double ml : o.ml) {
			const lab::glass::Vessel *g = lab::glass::find(o.kind, ml);
			REQUIRE(g != nullptr);
			REQUIRE(g->paths.size() == Vessel::PATHS + 1);
			for (const double share : { 0.0, 0.137, 0.5, 0.731, 0.96 }) {
				const double held = share * g->capacity();
				const double from = g->level(held);
				double direct = 0.0;
				for (int i = 0; i < 4000; ++i) {
					const double z = from + (g->height - from) * (i + 0.5) / 4000.0, r = std::max(g->radius_inside(z), 0.1 * g->mouth());
					direct += (g->height - from) / 4000.0 / (3.14159265358979323846 * r * r);
				}
				direct = std::max(direct, 0.3 / (3.14159265358979323846 * g->mouth() * g->mouth()));
				CAPTURE(o.kind);
				CAPTURE(ml);
				CAPTURE(share);
				CHECK(g->mouth_path(held) == doctest::Approx(direct).epsilon(0.02));
			}
			// fuller, a shorter path: the mouth exchanges faster
			CHECK(g->mouth_path(0.9 * g->capacity()) < g->mouth_path(0.1 * g->capacity()));
		}
	}
}

TEST_CASE("glassware: volumetric glassware is made to its standard's rows (Stage 52)") {
	// the burette (ISO 385): 50 mL between its zero line, 50 mm under its top, and its lowest line, at least 50 mm over
	// its stopcock; no longer than 820 mm; its bore uniform; a tip that empties its scale in its 80 s
	const Vessel &b = *find("burette", 50);
	CHECK(b.outlet);
	CHECK(b.height <= 82.0);
	CHECK(b.zero == doctest::Approx(b.height - 5.0));
	const double lowest = b.level(b.volume_below(b.zero) - 50.0);
	CHECK(b.zero - lowest == doctest::Approx(55.0).epsilon(1e-6)); // its scale: 550 mm
	CHECK(lowest >= 5.0 + 5.0 + 5.0 - 1e-9); // the jet, the stopcock and 50 mm under the lowest line
	CHECK(b.tip_bore > 0.03);
	CHECK(b.tip_bore < 0.15);
	CHECK(b.tolerance_ml == 0.05);
	CHECK(b.graduation_ml == 0.1);
	// the bulb pipette (ISO 648): no longer than 530 mm, its bulb at most 24 mm across, its mark in the suction tube,
	// whose inside there is at most 5.5 mm across
	const Vessel &p = *find("pipette", 25);
	CHECK(p.outlet);
	CHECK(p.height <= 53.0);
	CHECK(p.widest() <= 1.2 + 1e-9);
	CHECK(2.0 * p.radius_inside(p.level(25.0)) <= 0.55);
	CHECK(p.level(25.0) > 23.0 + 4.0); // over the bulb
	CHECK(p.tip_bore > 0.03);
	// volumetric flasks (ISO 1042): the mark in the neck's lower two thirds, its inside there 12-14 / 14-17 mm
	for (const auto &[ml, lo, hi] : { std::tuple{ 100.0, 1.2, 1.4 }, std::tuple{ 250.0, 1.4, 1.7 } }) {
		const Vessel &f = *find("volumetric_flask", ml);
		const double mark = f.level(ml);
		CHECK(2.0 * f.radius_inside(mark) >= lo);
		CHECK(2.0 * f.radius_inside(mark) <= hi);
		// where the neck's inside starts: the lowest height with the mark's radius
		double shoulder = mark;
		while (shoulder > 0.0 && std::fabs(f.radius_inside(shoulder - 0.01) - f.radius_inside(mark)) < 1e-6) {
			shoulder -= 0.01;
		}
		CHECK(mark < shoulder + 2.0 / 3.0 * (f.height - shoulder));
		CHECK(f.widest() == doctest::Approx(ml == 100.0 ? 3.0 : 4.0).epsilon(0.05)); // the bulb about the standard's
	}
	// measuring cylinders (ISO 4788): the nominal volume at the highest line, at least 20 / 25 / 35 mm under the top
	for (const auto &[ml, top, height] : { std::tuple{ 10.0, 2.0, 14.0 }, std::tuple{ 25.0, 2.5, 17.0 }, std::tuple{ 100.0, 3.5, 26.0 } }) {
		const Vessel &m = *find("measuring_cylinder", ml);
		CHECK(m.height <= height);
		CHECK(m.level(ml) <= m.height - top + 1e-9);
		CHECK(m.capacity() > ml);
	}
	// the foam cup: its 8 oz below its brim
	CHECK(find("foam_cup", 237)->capacity() > 237.0);
}

// (Stage 68) every shape glass::find gives is made at build time (chem_glassgen): each is glass::make's, to the last bit
TEST_CASE("glassware: every shape made at build time is the one glass::make makes, to the last bit (Stage 68)") {
	const nlohmann::json table = nlohmann::json::parse(GLASSWARE_JSON);
	const auto same_chain = [](const Chain &a, const Chain &b) {
		if (a.size() != b.size()) {
			return false;
		}
		for (size_t i = 0; i < a.size(); ++i) {
			const Piece &p = a[i], &q = b[i];
			if (p.arc != q.arc || p.a.r != q.a.r || p.a.y != q.a.y || p.b.r != q.b.r || p.b.y != q.b.y || p.centre.r != q.centre.r || p.centre.y != q.centre.y ||
					p.radius != q.radius || p.t0 != q.t0 || p.t1 != q.t1) {
				return false;
			}
		}
		return true;
	};
	int shapes = 0;
	for (const nlohmann::json &kind : table.at("kinds")) {
		for (const nlohmann::json &size : kind.at("sizes")) {
			const std::string name = kind.at("name").get<std::string>();
			const double ml = size.at("ml").get<double>();
			CAPTURE(name);
			CAPTURE(ml);
			const Vessel made = vessel(name.c_str(), ml);
			const Vessel *built = find(name, ml);
			REQUIRE(built != nullptr);
			CHECK(built->kind == made.kind);
			for (const auto &[a, b] : { std::pair{ built->nominal_ml, made.nominal_ml }, { built->diameter, made.diameter }, { built->height, made.height }, { built->wall, made.wall },
						 { built->floor, made.floor }, { built->pour_ml_s, made.pour_ml_s }, { built->cap_bore, made.cap_bore }, { built->burst, made.burst },
						 { built->marks_ml, made.marks_ml }, { built->marks_error, made.marks_error }, { built->tolerance_ml, made.tolerance_ml },
						 { built->graduation_ml, made.graduation_ml }, { built->delivery_s, made.delivery_s }, { built->tip_bore, made.tip_bore }, { built->zero, made.zero },
						 { built->stem, made.stem }, { built->cone_half_angle, made.cone_half_angle }, { built->paper_herzberg_s, made.paper_herzberg_s },
						 { built->paper_height, made.paper_height }, { built->capacity_cached, made.capacity_cached } }) {
				CHECK(std::memcmp(&a, &b, sizeof a) == 0);
			}
			CHECK(built->screw_cap == made.screw_cap);
			CHECK(built->outlet == made.outlet);
			CHECK(built->mark == made.mark);
			CHECK(same_chain(built->outside, made.outside));
			CHECK(same_chain(built->inside, made.inside));
			CHECK(built->paths == made.paths);
			++shapes;
		}
	}
	CHECK(shapes >= 14);
}
