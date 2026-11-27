// The measured lab (GAME_SPEC §13, the balance and Readings; Stage 50): a balance under a vessel reads its load's
// conventional mass and changes nothing in the world; by instruments a vessel reads its thermometer, its pH meter and
// its marks within their errors (I1); a hand misses what it aims at by its share, the same on every run.
#include "lab/instruments.hpp"
#include "lab/lab.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <string>

namespace {

// a bench of a balance, two beakers, a jar of the pentahydrate, a burner; `extra` is the bench's own fields
std::string bench(const std::string &extra = "", const std::string &model = "top_loading") {
	return R"({"format": "chem-bench/2", "grid": [28, 15], "room": {"celsius": 20, "air_mol": 1000},)" + extra + R"(
		"items": [
			{"id": "scale", "kind": "balance", "model": ")" + model + R"(", "at": [1, 12]},
			{"id": "beaker", "kind": "vessel", "glassware": "beaker", "volume_ml": 250, "wall": "glass", "wall_g": 80, "at": [8, 7],
				"label": "water", "contents": {"water": {"g": 100}}},
			{"id": "dry", "kind": "vessel", "glassware": "beaker", "volume_ml": 250, "wall": "glass", "wall_g": 80, "at": [12, 7]},
			{"id": "gas", "kind": "vessel", "glassware": "beaker", "volume_ml": 250, "wall": "glass", "wall_g": 80, "at": [16, 7],
				"contents": {"carbon_dioxide": {"mol": 0.004}}},
			{"id": "jar", "kind": "vessel", "glassware": "beaker", "volume_ml": 50, "wall": "glass", "wall_g": 40, "at": [20, 9],
				"label": "copper sulfate, blue crystals", "contents": {"copper_sulfate_pentahydrate": {"g": 10, "particles": 100000}, "copper_oxide": {"g": 2, "particles": 100000}}},
			{"id": "acid", "kind": "vessel", "glassware": "beaker", "volume_ml": 50, "wall": "glass", "wall_g": 40, "at": [23, 9],
				"contents": {"water": {"g": 24.05}, "sulfuric_acid_aq": {"mol": 0.025}}},
			{"id": "flame", "kind": "burner", "celsius": 1927, "conductance": 0.25}
		]})";
}

std::unique_ptr<lab::Lab> open(const std::string &text) {
	std::string why;
	auto made = lab::Lab::open(text, why);
	REQUIRE_MESSAGE(made != nullptr, why);
	return made;
}

void must(lab::Lab &l, const std::string &command) {
	std::string why;
	REQUIRE_MESSAGE(l.command(command, why), command << ": " << why);
}

std::string refused(lab::Lab &l, const std::string &command) {
	std::string why;
	const std::string before = l.digest(), saved = l.save();
	CHECK_MESSAGE(!l.command(command, why), command);
	CHECK(l.digest() == before);
	CHECK(l.save() == saved);
	return why;
}

lab::ItemView item(const lab::LabView &v, const std::string &id) {
	for (const lab::ItemView &i : v.items) {
		if (i.id == id) {
			return i;
		}
	}
	FAIL("no item " << id);
	return lab::ItemView{};
}

// the closed form: what a vessel weighs on a balance, from its view alone (its glass at ISO 3585's 2.23 g/cm^3,
// what it holds by its own volume) and the room's air from its composition (79 % N2, 21 % O2) at 1 atm and 20 C
double closed_form(const lab::ItemView &v) {
	double g = 0.0, cm3 = 0.0;
	for (const lab::SubstanceView &s : v.contents) {
		g += s.g;
		cm3 += s.wall ? s.g / 2.23 : s.ml;
	}
	const double molar = 0.79 * 28.0134 + 0.21 * 31.9988, air = 101325.0 * molar * 1e-3 / (8.314462618 * 293.15);
	return (g - air * 1e-3 * cm3) / (1.0 - air / 8000.0);
}

} // namespace

TEST_CASE("lab: a balance reads its load's conventional mass (Stage 50)") {
	auto l = open(bench());
	CHECK(std::fabs(l->view().air_kg_m3 - 1.1994) < 0.0005); // the room's air: nitrogen and oxygen, no argon (28.85 g/mol)
	lab::ItemView s = item(l->view(), "scale");
	CHECK(s.kind == "balance");
	CHECK(s.w == 4);
	CHECK(s.h == 2);
	CHECK(s.display_g == 0.0); // an empty pan
	must(*l, R"({"do": "weigh", "balance": "scale", "vessel": "beaker"})");
	s = item(l->view(), "scale");
	CHECK(s.place == "under");
	CHECK(s.under >= 0);
	const lab::ItemView beaker = item(l->view(), "beaker");
	const double expected = closed_form(beaker);
	CHECK(std::fabs(s.gross_g - expected) < 1e-5);
	REQUIRE(s.display_g);
	CHECK(std::fabs(*s.display_g - expected) <= s.linearity_g + s.readability_g / 2 + 1e-9);
	// 80 g of glass and 100 g of water read about 179.85 g: less by the air they displace
	CHECK(*s.display_g < 180.0);
	CHECK(*s.display_g > 179.7);
	// tared, it reads 0; 25 mL poured out, it reads what left, as a conventional mass
	must(*l, R"({"do": "tare", "balance": "scale"})");
	CHECK(item(l->view(), "scale").display_g == 0.0);
	must(*l, R"({"do": "pour", "from": "beaker", "to": "dry", "ml": 25})");
	s = item(l->view(), "scale");
	CHECK(std::fabs(*s.display_g - (closed_form(item(l->view(), "beaker")) - expected)) <= s.linearity_g + s.readability_g / 2 + 1e-9);
	CHECK(*s.display_g < -24.5);
	// the analytical balance: to 0.1 mg
	auto a = open(bench("", "analytical"));
	must(*a, R"({"do": "weigh", "balance": "scale", "vessel": "jar"})");
	const lab::ItemView fine = item(a->view(), "scale");
	CHECK(fine.readability_g == 0.0001);
	CHECK(std::fabs(*fine.display_g - closed_form(item(a->view(), "jar"))) <= fine.linearity_g + fine.readability_g / 2 + 1e-9);
	CHECK(std::fabs(*fine.display_g * 1e4 - std::round(*fine.display_g * 1e4)) < 1e-6);
}

TEST_CASE("lab: a vessel's hot air reads light, its carbon dioxide heavy (Stage 50)") {
	// the same beaker, of air and of carbon dioxide: CO2 (44 g/mol) is denser than the air it displaces
	auto l = open(bench());
	must(*l, R"({"do": "weigh", "balance": "scale", "vessel": "dry"})");
	const double air_only = item(l->view(), "scale").gross_g;
	must(*l, R"({"do": "unweigh", "balance": "scale"})");
	must(*l, R"({"do": "weigh", "balance": "scale", "vessel": "gas"})");
	const double co2 = item(l->view(), "scale").gross_g;
	// its CO2 for as much of the air: n (44.01 - 28.96) g (Stage 64: the bench opens exactly full, so of the 0.004 mol it
	// states what a 250 mL beaker holds with its air, 0.0031 mol, near 0.047 g)
	double co2_mol = 0.0;
	for (const lab::SubstanceView &sub : item(l->view(), "gas").contents) {
		co2_mol += sub.species == "carbon_dioxide" ? sub.mol : 0.0;
	}
	CHECK(co2_mol > 0.003);
	CHECK(co2 - air_only == doctest::Approx(co2_mol * (44.01 - 28.96)).epsilon(0.1));
	// the dry beaker heated, then weighed hot: its air is thinner, so it reads less than cold
	must(*l, R"({"do": "unweigh", "balance": "scale"})");
	must(*l, R"({"do": "heat", "burner": "flame", "vessel": "dry"})");
	must(*l, R"({"do": "valve", "burner": "flame", "to": 1})");
	must(*l, R"({"do": "wait", "seconds": 300})");
	must(*l, R"({"do": "unheat", "burner": "flame"})");
	must(*l, R"({"do": "weigh", "balance": "scale", "vessel": "dry"})");
	const lab::ItemView hot = item(l->view(), "dry");
	CHECK(hot.celsius > 100.0);
	const double light = item(l->view(), "scale").gross_g;
	CHECK(light < air_only - 0.01);
	CHECK(std::fabs(light - closed_form(hot)) < 1e-5);
}

TEST_CASE("lab: a balance joins nothing in the world, is left where its vessel stood, and is saved (Stage 50)") {
	auto with = open(bench());
	auto without = open(bench());
	must(*with, R"({"do": "weigh", "balance": "scale", "vessel": "beaker"})");
	must(*with, R"({"do": "tare", "balance": "scale"})");
	CHECK(with->digest() == without->digest());
	for (lab::Lab *l : { with.get(), without.get() }) {
		must(*l, R"({"do": "pour", "from": "beaker", "to": "dry", "ml": 10})");
		must(*l, R"({"do": "wait", "seconds": 5})");
	}
	CHECK(with->digest() == without->digest());
	// refusals: one thing under a vessel; a balance under a balloon or under nothing
	CHECK(refused(*with, R"({"do": "heat", "burner": "flame", "vessel": "beaker"})").find("stands over") != std::string::npos);
	must(*with, R"({"do": "heat", "burner": "flame", "vessel": "dry"})");
	CHECK(refused(*with, R"({"do": "weigh", "balance": "scale", "vessel": "dry"})").find("already under") != std::string::npos);
	CHECK(!refused(*with, R"({"do": "weigh", "balance": "flame", "vessel": "jar"})").empty());
	CHECK(!refused(*with, R"({"do": "tare", "balance": "beaker"})").empty());
	// saved and loaded: its model, its tare, where it is
	std::string why;
	auto loaded = lab::Lab::load(with->save(), why);
	REQUIRE_MESSAGE(loaded != nullptr, why);
	CHECK(loaded->save() == with->save());
	CHECK(loaded->digest() == with->digest());
	CHECK(item(loaded->view(), "scale").tare_g == item(with->view(), "scale").tare_g);
	CHECK(item(loaded->view(), "beaker").label == "water");
	// the vessel lifted off and set elsewhere: the balance stays where it stood, on the bench, reading nothing
	const lab::ItemView was = item(with->view(), "beaker");
	must(*with, R"({"do": "place", "item": "beaker", "at": [8, 0]})");
	const lab::ItemView left = item(with->view(), "scale");
	CHECK(left.place == "bench");
	CHECK(left.under == -1);
	CHECK(left.row + left.h == was.row + was.h); // its foot where the vessel's was
	CHECK(*left.display_g < 0.0); // still tared to what was on it
	// moved with a group, a balance under a vessel goes with it
	must(*with, R"({"do": "weigh", "balance": "scale", "vessel": "beaker"})");
	must(*with, R"({"do": "move", "items": ["beaker"], "by": [0, 1]})");
	CHECK(item(with->view(), "scale").place == "under");
	// stowed, it goes with the vessel's leaving no further than where it stood
	must(*with, R"({"do": "stow", "item": "beaker"})");
	CHECK(item(with->view(), "scale").place == "bench");
	// handed in, too
	must(*with, R"({"do": "place", "item": "beaker", "at": [4, 0]})");
	must(*with, R"({"do": "weigh", "balance": "scale", "vessel": "beaker"})");
	must(*with, R"({"do": "submit", "item": "beaker"})");
	CHECK(item(with->view(), "scale").place == "bench");
	CHECK(item(with->view(), "scale").under == -1);
}

TEST_CASE("lab: by instruments a vessel reads its thermometer, its pH meter and its marks within their errors (I1) (Stage 50)") {
	using namespace lab::instruments;
	auto truth = open(bench());
	CHECK(!truth->view().instruments);
	for (const uint64_t seed : { 1u, 7u, 12345u }) {
		auto l = open(bench(R"( "readings": "instruments", "seed": )" + std::to_string(seed) + ","));
		const lab::LabView v = l->view();
		CHECK(v.instruments);
		CHECK(v.seed == seed);
		std::vector<Read> reads;
		for (const lab::ItemView &i : v.items) {
			if (i.kind != "vessel") {
				continue;
			}
			REQUIRE(i.read_c);
			reads.push_back(Read{ i.id + ": thermometer", i.celsius, *i.read_c, thermometer().accuracy, thermometer().resolution });
			if (i.ph) {
				REQUIRE(i.read_ph);
				reads.push_back(Read{ i.id + ": pH meter", *i.ph, *i.read_ph, ph_meter().accuracy, ph_meter().resolution });
			}
			REQUIRE(i.read_ml);
			reads.push_back(Read{ i.id + ": marks", i.liquid_ml, *i.read_ml, 0.05 * i.liquid_ml, i.marks_ml });
		}
		CHECK(reads.size() == 12); // five vessels' thermometers and marks, two pH meters
		CHECK(check_I1(reads) == "");
		// the same bench, the same instruments; the truth's lab reads its own, the same seed 0's
		CHECK(*item(v, "beaker").read_c == *item(open(bench(R"( "readings": "instruments", "seed": )" + std::to_string(seed) + ","))->view(), "beaker").read_c);
		// saved and loaded, still by instruments, and the seed digested
		std::string why;
		auto loaded = lab::Lab::load(l->save(), why);
		REQUIRE_MESSAGE(loaded != nullptr, why);
		CHECK(loaded->view().instruments);
		CHECK(loaded->digest() == l->digest());
		CHECK(l->digest() != truth->digest());
	}
	// a bench of the truth with no seed saves and digests as one from before: no readings, no seed
	CHECK(truth->save().find("\"readings\"") == std::string::npos);
	// refused: readings of neither kind; a seed below 0
	std::string why;
	CHECK(lab::Lab::open(bench(R"( "readings": "guess",)"), why) == nullptr);
	CHECK(lab::Lab::open(bench(R"( "seed": -3,)"), why) == nullptr);
}

TEST_CASE("lab: a hand misses an amount by up to its share, the same on every run (Stage 50)") {
	auto l = open(bench(R"( "seed": 3,)"));
	auto again = open(bench(R"( "seed": 3,)"));
	for (lab::Lab *x : { l.get(), again.get() }) {
		must(*x, R"({"do": "tip", "from": "jar", "to": "dry", "species": "copper_sulfate_pentahydrate", "g": 2, "about": true})");
		must(*x, R"({"do": "pour", "from": "beaker", "to": "gas", "ml": 20, "about": true})");
	}
	CHECK(l->digest() == again->digest());
	double tipped = 0.0;
	for (const lab::SubstanceView &s : item(l->view(), "dry").contents) {
		tipped += s.species == "copper_sulfate_pentahydrate" ? s.g : 0.0;
	}
	CHECK(tipped >= 1.8 - 1e-6);
	CHECK(tipped <= 2.2 + 1e-6);
	CHECK(std::fabs(tipped - 2.0) > 1e-3); // it missed
	const double poured = item(l->view(), "gas").liquid_ml;
	CHECK(poured > 20.0 * 0.9 - 0.5);
	CHECK(poured < 20.0 * 1.1 + 0.5);
	// another seed misses otherwise; with no "about", exactly
	auto other = open(bench(R"( "seed": 4,)"));
	must(*other, R"({"do": "tip", "from": "jar", "to": "dry", "species": "copper_sulfate_pentahydrate", "g": 2, "about": true})");
	double other_g = 0.0;
	for (const lab::SubstanceView &s : item(other->view(), "dry").contents) {
		other_g += s.species == "copper_sulfate_pentahydrate" ? s.g : 0.0;
	}
	CHECK(other_g != tipped);
	auto exact = open(bench());
	must(*exact, R"({"do": "tip", "from": "jar", "to": "dry", "species": "copper_sulfate_pentahydrate", "g": 2})");
	for (const lab::SubstanceView &s : item(exact->view(), "dry").contents) {
		if (s.species == "copper_sulfate_pentahydrate") {
			CHECK(std::fabs(s.g - 2.0) < 1e-6);
		}
	}
	// a tip with no species: the solids in proportion to their masses
	must(*exact, R"({"do": "tip", "from": "jar", "to": "acid", "g": 3})");
	double crystals = 0.0, oxide = 0.0;
	for (const lab::SubstanceView &s : item(exact->view(), "acid").contents) {
		crystals += s.species == "copper_sulfate_pentahydrate" ? s.g : 0.0;
		oxide += s.species == "copper_oxide" ? s.g : 0.0;
	}
	CHECK(crystals + oxide > 2.9); // (some may dissolve at once: nothing does within the command)
	CHECK(std::fabs(crystals / oxide - 4.0) < 0.01); // 8 g of crystals left (2 g were tipped above) to 2 g of oxide
	// more than there is, aimed at by a hand, takes all; asked exactly, it is refused
	CHECK(!refused(*exact, R"({"do": "tip", "from": "jar", "to": "dry", "g": 50})").empty());
	must(*exact, R"({"do": "tip", "from": "jar", "to": "dry", "g": 50, "about": true})");
	for (const lab::SubstanceView &s : item(exact->view(), "jar").contents) {
		CHECK((s.phase != 's' || s.wall));
	}
}

TEST_CASE("lab: a vessel set onto a balance stands on it where the balance stood (Stage 50)") {
	auto l = open(bench());
	const lab::ItemView scale = item(l->view(), "scale");
	REQUIRE(scale.place == "bench");
	must(*l, R"({"do": "weigh", "balance": "scale", "vessel": "jar", "onto": true})");
	const lab::ItemView jar = item(l->view(), "jar");
	CHECK(jar.row + jar.h == scale.row + scale.h); // its foot on the balance's
	CHECK(jar.col == scale.col + (scale.w - jar.w) / 2);
	CHECK(item(l->view(), "scale").place == "under");
	CHECK(*item(l->view(), "scale").display_g > 50.0);
	// from the cabinet: a vessel stowed, then set onto the balance left on the bench
	must(*l, R"({"do": "stow", "item": "jar"})");
	CHECK(item(l->view(), "scale").place == "bench");
	must(*l, R"({"do": "weigh", "balance": "scale", "vessel": "jar", "onto": true})");
	CHECK(item(l->view(), "jar").place == "bench");
	CHECK(item(l->view(), "scale").under >= 0);
	// refused: onto a balance that is not on the bench; a vessel too tall for the bench over it
	must(*l, R"({"do": "unweigh", "balance": "scale"})");
	must(*l, R"({"do": "stow", "item": "scale"})");
	CHECK(refused(*l, R"({"do": "weigh", "balance": "scale", "vessel": "beaker", "onto": true})").find("not standing") != std::string::npos);
	must(*l, R"({"do": "place", "item": "scale", "at": [1, 0]})");
	CHECK(!refused(*l, R"({"do": "weigh", "balance": "scale", "vessel": "beaker", "onto": true})").empty());
	// a vessel in storage without "onto" is refused, as a burner's (Stage 61: storage, where the cabinet was)
	must(*l, R"({"do": "stow", "item": "dry"})");
	CHECK(refused(*l, R"({"do": "weigh", "balance": "scale", "vessel": "dry"})").find("storage") != std::string::npos);
}

TEST_CASE("instruments: a balance's display draws its reading in seven segments (Stage 50)") {
	using namespace lab::instruments;
	const Balance &top = *balance("top_loading"), &fine = *balance("analytical");
	CHECK(display_text(top, 12.346, false) == "12.35 g");
	CHECK(display_text(fine, 12.345, false) == "12.3450 g");
	CHECK(display_text(top, -0.004, false) == "0.00 g");
	CHECK(display_text(top, 700.0, true) == "OL");
	CHECK(display_text(top, std::nullopt, false).empty());
	lab::Picture a, b, off;
	plot_display("12.35 g", a);
	plot_display("12.36 g", b);
	plot_display("", off);
	CHECK(a.hash != b.hash);
	CHECK(a.w == 96);
	int lit = 0, dark = 0;
	for (size_t k = 0; k + 2 < a.rgb.size(); k += 3) {
		lit += a.rgb[k + 1] > 200 ? 1 : 0;
	}
	for (const uint8_t c : off.rgb) {
		dark += c == 0 ? 1 : 0;
	}
	CHECK(lit > 40);
	CHECK(dark == static_cast<int>(off.rgb.size()));
	// the unit is no digit (the user, 2026-11-25: a tared balance read "0.009"): "0.00 g" lights none of a 9's segments
	// where a digit would stand after the zeros, and differs from "0.009"
	lab::Picture zero, nine;
	plot_display("0.00 g", zero);
	plot_display("0.009", nine);
	CHECK(zero.hash != nine.hash);
	int unit_lit = 0, nine_lit = 0;
	for (int y = 0; y < zero.h; ++y) {
		for (int x = zero.w - 8; x < zero.w; ++x) {
			unit_lit += zero.at(x, y)[1] > 200 ? 1 : 0; // the digits' own bright green
			nine_lit += nine.at(x, y)[1] > 200 ? 1 : 0;
		}
	}
	CHECK(unit_lit == 0); // the unit is a dimmer legend, not lit as a digit
	CHECK(nine_lit > 10);
}
