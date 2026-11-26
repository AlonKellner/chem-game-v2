// The debug panel (GAME_SPEC §13.6): an item from the inside, its flows in the
// last step, and each reaction's account of itself and of what limits it.
#include "lab/lab.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string read(const std::string &relative) {
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/" + relative);
	REQUIRE_MESSAGE(static_cast<bool>(in), relative);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

// one vessel holding `contents`, and a burner beside it
std::unique_ptr<lab::Lab> one(const std::string &contents) {
	const std::string bench = R"({"format": "chem-bench/2", "room": {"celsius": 20}, "items": [
		{"id": "flask", "at": [0, 0], "kind": "vessel", "volume_ml": 250, "wall": "glass", "wall_g": 100, "loss": 0.9, "contents": )" +
			contents + R"(},
		{"id": "burner", "at": [5, 0], "kind": "burner", "celsius": 1927, "conductance": 0.25}]})";
	std::string why;
	auto l = lab::Lab::open(bench, why);
	REQUIRE_MESSAGE(l != nullptr, why);
	l->set_checking(false);
	return l;
}

void must(lab::Lab &l, const std::string &command) {
	std::string why;
	REQUIRE_MESSAGE(l.command(command, why), command << ": " << why);
}

const lab::ReactionDebug &reaction(const lab::ItemDebug &d, const std::string &id) {
	const auto r = std::find_if(d.reactions.begin(), d.reactions.end(), [&](const lab::ReactionDebug &x) { return x.id == id; });
	REQUIRE_MESSAGE(r != d.reactions.end(), id);
	return *r;
}

bool begins(const std::string &text, const std::string &start) {
	return text.rfind(start, 0) == 0;
}

} // namespace

TEST_CASE("debug: what an item holds: amounts, mole fractions in their own mixture, molality, activity (Stage 18)") {
	auto l = one(R"({"water": {"g": 100}, "sulfuric_acid_aq": {"mol": 0.1}, "copper_oxide": {"g": 1, "particles": 1000}})");
	const lab::ItemDebug d = l->debug("flask");
	REQUIRE(d.found);
	CHECK(d.has_temperature);
	CHECK(d.celsius == doctest::Approx(20.0).epsilon(1e-6));
	CHECK(d.heat_capacity > 400.0); // 100 g of water, and the glass
	double gas = 0.0;
	for (const lab::SpeciesDebug &s : d.species) {
		gas += s.phase == 'g' ? s.fraction : 0.0;
		if (s.species == "sulfuric_acid_aq") {
			CHECK(s.mol == doctest::Approx(0.1).epsilon(1e-9));
			CHECK(s.molality == doctest::Approx(0.1 / 0.1).epsilon(1e-3)); // 0.1 mol in 100 g of water
			// an electrolyte (Stage 19): (2m gamma)^2 (m gamma), Staples' gamma 0.1247 at 1 mol/kg
			CHECK(s.activity == doctest::Approx(4.0 * 0.1247 * 0.1247 * 0.1247).epsilon(0.03));
			CHECK(s.formula == "H2SO4(aq)");
		}
		if (s.species == "copper_oxide") {
			CHECK(s.fraction == 1.0);
			CHECK(s.activity == 1.0);
			CHECK(s.g == doctest::Approx(1.0).epsilon(1e-6));
		}
		CHECK(s.species != "glass"); // the vessel's own wall is not what it holds
	}
	CHECK(gas == doctest::Approx(1.0).epsilon(1e-12)); // the flask's air
	CHECK_FALSE(l->debug("kettle").found);
	CHECK_FALSE(l->debug("burner").found);
}

TEST_CASE("debug: each reaction says why it happens and what limits it: too cold, used up, its surface, the supersaturation (Stage 18)") {
	SUBCASE("malachite, cold and then hot") {
		auto l = one(R"({"malachite": {"g": 5, "particles": 2500000}})");
		lab::ReactionDebug r = reaction(l->debug("flask"), "malachite_decomposition");
		CHECK(r.equation == "Cu2CO3(OH)2(s) -> 2 CuO(s) + CO2(g) + H2O(g)");
		CHECK(r.explain.find("carbon dioxide") != std::string::npos);
		INFO(r.limit);
		CHECK(begins(r.limit, "too cold: "));
		CHECK(r.limit.find("fast from about 29") != std::string::npos); // k e^(-Ea/RT) = 1/60 s near 295 C (Stage 23: Koga's)
		must(*l, R"({"do": "heat", "burner": "burner", "vessel": "flask"})");
		must(*l, R"({"do": "valve", "burner": "burner", "to": 1})");
		must(*l, R"({"do": "wait_until", "item": "flask", "above_c": 300, "timeout": 3600})");
		const lab::ItemDebug d = l->debug("flask");
		r = reaction(d, "malachite_decomposition");
		INFO(r.limit);
		CHECK(r.rate_mol_s > 0.0);
		CHECK(begins(r.limit, "fast: half of it goes in "));
		CHECK(r.affinity_kj > 0.0);
		// its flows: heat from the burner in, to the room out, and gas pushed out to the room's air
		bool burner = false, room = false, gas = false;
		for (const lab::FlowDebug &f : d.flows) {
			burner = burner || (f.with == "burner" && f.process == "heat" && f.watts > 0.0);
			room = room || (f.with == "room" && f.process == "heat" && f.watts < 0.0);
			gas = gas || (f.with == "air" && f.process == "settle" && f.grams_per_s < 0.0);
		}
		CHECK(burner);
		CHECK(room);
		CHECK(gas);
		std::string why;
		for (int i = 0; i < 120 && reaction(l->debug("flask"), "malachite_decomposition").limit != "no Cu2CO3(OH)2(s) left"; ++i) {
			REQUIRE(l->advance(60 * 10, why)); // to the last quantum
		}
		CHECK(reaction(l->debug("flask"), "malachite_decomposition").limit == "no Cu2CO3(OH)2(s) left");
	}
	SUBCASE("copper oxide: no acid, then its surface, which stirring hardly speeds, then stirred") {
		auto dry = one(R"({"copper_oxide": {"g": 2, "particles": 100000}})");
		CHECK(reaction(dry->debug("flask"), "copper_oxide_dissolution").limit == "no H+(aq) left"); // (Stage 63: the acid's driver its hydrogen ions)
		auto l = one(R"({"water": {"g": 100}, "sulfuric_acid_aq": {"mol": 0.1}, "copper_oxide": {"g": 2, "particles": 100000}})");
		const lab::ReactionDebug r = reaction(l->debug("flask"), "copper_oxide_dissolution");
		INFO(r.limit);
		CHECK(begins(r.limit, "limited at its surface: ")); // the reaction there, not the acid reaching it (Stage 23)
		CHECK(r.limit.find("cm2 of CuO(s), 2 mol/kg of H+(aq)") != std::string::npos);
		CHECK(r.limit.find("; stirring would hardly speed it") != std::string::npos);
		must(*l, R"({"do": "stir", "vessel": "flask", "on": true})");
		CHECK(r.limit != reaction(l->debug("flask"), "copper_oxide_dissolution").limit);
		CHECK(reaction(l->debug("flask"), "copper_oxide_dissolution").limit.find("; stirred") != std::string::npos);
	}
	SUBCASE("crystals: supersaturated with none yet, then growing; water held at its equilibrium") {
		auto l = one(R"({"water": {"g": 50}, "copper_sulfate_aq": {"mol": 0.3}})");
		lab::ReactionDebug r = reaction(l->debug("flask"), "pentahydrate_crystallization");
		INFO(r.limit);
		CHECK(begins(r.limit, "supersaturated (S = "));
		CHECK(r.limit.find("no crystals yet") != std::string::npos);
		const lab::ReactionDebug w = reaction(l->debug("flask"), "water_vaporization");
		CHECK((w.limit == "at equilibrium" || w.limit == "fast: held at its equilibrium every step"));
		std::string why;
		REQUIRE(l->advance(60 * 5, why));
		r = reaction(l->debug("flask"), "pentahydrate_crystallization");
		INFO(r.limit);
		CHECK((r.limit.find("growing on ") != std::string::npos || r.limit == "at equilibrium"));
		CHECK(r.rate_mol_s >= 0.0);
	}
	SUBCASE("dry crystals: the first dehydration too cold, fast from about 94 C (Stage 19)") {
		auto l = one(R"({"copper_sulfate_pentahydrate": {"g": 5, "particles": 1000}})");
		const lab::ReactionDebug c = reaction(l->debug("flask"), "pentahydrate_dehydration");
		INFO(c.limit);
		CHECK(begins(c.limit, "too cold: "));
		CHECK(c.limit.find("fast from about 9") != std::string::npos); // about 94 C
	}
}

TEST_CASE("debug: the flows are the last step's, the same with checking on or off (Stage 18)") {
	auto checked = one(R"({"malachite": {"g": 5, "particles": 2500000}})"), unchecked = one(R"({"malachite": {"g": 5, "particles": 2500000}})");
	checked->set_checking(true);
	for (lab::Lab *l : { checked.get(), unchecked.get() }) {
		must(*l, R"({"do": "heat", "burner": "burner", "vessel": "flask"})");
		must(*l, R"({"do": "valve", "burner": "burner", "to": 1})");
		must(*l, R"({"do": "wait", "seconds": 120})");
	}
	const lab::ItemDebug a = checked->debug("flask"), b = unchecked->debug("flask");
	REQUIRE(a.flows.size() == b.flows.size());
	for (size_t i = 0; i < a.flows.size(); ++i) {
		CHECK(a.flows[i].with == b.flows[i].with);
		CHECK(a.flows[i].watts == b.flows[i].watts);
		CHECK(a.flows[i].grams_per_s == b.flows[i].grams_per_s);
	}
	CHECK(reaction(a, "malachite_decomposition").rate_mol_s == reaction(b, "malachite_decomposition").rate_mol_s);
}

TEST_CASE("debug: on the screen: the panel for the item selected, a single step, an export that loads to the same digest (Stage 18)") {
	std::string why;
	auto ui = lab::Ui::open(read("data/benches/demo.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	const auto click = [&](const std::string &what) {
		float x = 0, y = 0;
		REQUIRE_MESSAGE(ui->locate(what, x, y), what);
		ui->pointer(x, y, true);
		ui->pointer(x, y, false);
	};
	const auto drawn = [&](const std::string &text) { return ui->draw().text.find(text) != std::string::npos; };
	for (const char *c : { R"({"do": "heat", "burner": "burner", "vessel": "flask"})", R"({"do": "tip", "from": "jar", "to": "flask", "species": "malachite", "all": true})",
				 R"({"do": "valve", "burner": "burner", "to": 1})" }) {
		REQUIRE(ui->lab().command(c, why));
	}
	float x = 0, y = 0;
	CHECK_FALSE(ui->locate("debug:step", x, y));
	click("debug");
	CHECK(drawn("select an item to look inside it"));
	click("item:flask");
	CHECK(drawn("inside the selected item"));
	CHECK(drawn("Cu2CO3(OH)2(s) -> 2 CuO(s) + CO2(g) + H2O(g)"));
	CHECK(drawn("limit: too cold: "));
	// a single step, the clock paused
	const int64_t before = ui->lab().step();
	click("debug:step");
	CHECK(ui->lab().step() == before + 1);
	CHECK(ui->paused());
	click("debug:step");
	CHECK(ui->lab().step() == before + 2);
	CHECK(drawn("heat with flask.floor:")); // (Stage 53: the flame heats its floor, which heats what it holds)
	// scrolled, and back
	click("debug:down");
	CHECK_FALSE(drawn("what it holds: mol, g"));
	click("debug:up");
	CHECK(drawn("what it holds: mol, g"));
	// exported: the save, once; loaded, the same digests on
	CHECK(ui->take_export().empty());
	click("debug:export");
	const std::string save = ui->take_export();
	REQUIRE_FALSE(save.empty());
	CHECK(ui->take_export().empty());
	auto again = lab::Lab::load(save, why);
	REQUIRE_MESSAGE(again != nullptr, why);
	CHECK(again->digest() == ui->lab().digest());
	REQUIRE(ui->lab().advance(600, why));
	REQUIRE(again->advance(600, why));
	CHECK(again->digest() == ui->lab().digest());
	click("debug:close");
	CHECK_FALSE(ui->locate("debug:step", x, y));
}

TEST_CASE("debug: under a tutorial, a single step only while the tutorial lets the clock run (Stage 18)") {
	std::string why;
	auto ui = lab::Ui::open(read("data/benches/lab.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	REQUIRE_MESSAGE(ui->tutor(read("data/tutorials/demo.json"), why), why);
	float x = 0, y = 0;
	REQUIRE(ui->locate("debug", x, y));
	ui->pointer(x, y, true);
	ui->pointer(x, y, false);
	REQUIRE(ui->locate("debug:step", x, y));
	const int64_t before = ui->lab().step();
	ui->pointer(x, y, true);
	ui->pointer(x, y, false);
	CHECK(ui->lab().step() == before); // its first step asks for a gesture
	CHECK(ui->message().find("The clock waits for you") != std::string::npos);
}

TEST_CASE("debug: a vessel's pressure, a pour as a pour (its kind, rate, cap and head), and what a signal part is doing (Stage 28)") {
	std::string why;
	auto l = lab::Lab::open(read("data/benches/demo.json"), why);
	REQUIRE_MESSAGE(l != nullptr, why);
	must(*l, R"({"do": "invert", "item": "acid", "over": "flask"})");
	must(*l, R"({"do": "cap", "item": "acid", "to": 0.5})");
	REQUIRE(l->advance(2, why));
	const lab::ItemDebug acid = l->debug("acid");
	const auto has = [](const lab::ItemDebug &d, const std::string &words) {
		return std::any_of(d.facts.begin(), d.facts.end(), [&](const std::string &f) { return f.find(words) != std::string::npos; });
	};
	CHECK(has(acid, "open to the room: 101.3 kPa"));
	CHECK(has(acid, "pours into flask upside down: its cap 50% open under"));
	const lab::ItemDebug flask = l->debug("flask");
	CHECK(std::any_of(flask.flows.begin(), flask.flows.end(), [](const lab::FlowDebug &f) { return f.process == "pour" && f.with == "acid" && f.grams_per_s > 0.0; }));
	// a controller and a notice part, from the inside
	must(*l, R"({"do": "part", "kind": "notice", "at": [0, 0]})");
	must(*l, R"({"do": "set", "part": "notice1", "level": "event", "contains": "boils", "seconds": 3})");
	const lab::ItemDebug notice = l->debug("notice1");
	CHECK(notice.found);
	CHECK(notice.part);
	CHECK(has(notice, "containing \"boils\""));
	CHECK(has(notice, "signals for 3.00 s after one: off"));
	must(*l, R"({"do": "unpour", "item": "acid"})");
	must(*l, R"({"do": "part", "kind": "controller", "at": [1, 0]})");
	must(*l, R"({"do": "sense", "part": "controller1", "item": "flask"})");
	must(*l, R"({"do": "heat", "burner": "burner", "vessel": "flask"})");
	must(*l, R"({"do": "rig", "from": "controller1", "item": "burner", "control": "valve"})");
	must(*l, R"({"do": "set", "part": "controller1", "what": "celsius", "target": 60, "speed": "normal"})");
	REQUIRE_MESSAGE(l->advance(60, why), why);
	const lab::ItemDebug c = l->debug("controller1");
	CHECK(has(c, "holds flask: temperature (C) at 60.0 C, normal"));
	CHECK(has(c, "its trial found:"));
	CHECK(has(c, "gain "));
	CHECK(has(c, "reads "));
}
