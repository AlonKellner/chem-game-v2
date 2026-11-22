// Sensing (GAME_SPEC §13.3, Stage 26): a monitor senses one item; what it reads is a quantity of that item,
// chosen as a kind, a material, a measure, from what the item has now.
#include "lab/conditions.hpp"
#include "lab/lab.hpp"

#include <doctest/doctest.h>

#include <algorithm>
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

std::unique_ptr<lab::Lab> demo() {
	std::string why;
	auto l = lab::Lab::open(read("data/benches/demo.json"), why);
	REQUIRE_MESSAGE(l != nullptr, why);
	return l;
}

void ok(lab::Lab &l, const std::string &command) {
	std::string why;
	REQUIRE_MESSAGE(l.command(command, why), command << ": " << why);
}

std::string refused(lab::Lab &l, const std::string &command) {
	std::string why;
	CHECK_MESSAGE(!l.command(command, why), command);
	return why;
}

const lab::ItemView &item(const lab::LabView &v, const std::string &id) {
	const auto at = std::find_if(v.items.begin(), v.items.end(), [&](const lab::ItemView &x) { return x.id == id; });
	REQUIRE(at != v.items.end());
	return *at;
}

} // namespace

TEST_CASE("sensing: a monitor senses one item; sensing another forgets its quantity; set reads what it senses (Stage 26)") {
	auto l = demo();
	ok(*l, R"({"do": "tip", "species": "malachite", "from": "jar", "to": "flask", "all": true})");
	ok(*l, R"({"do": "part", "kind": "monitor", "at": [1, 1]})");
	CHECK(refused(*l, R"({"do": "set", "part": "monitor1", "what": "celsius", "compare": "above", "value": 50})").find("senses nothing") != std::string::npos);
	ok(*l, R"({"do": "sense", "part": "monitor1", "item": "flask"})");
	CHECK(item(l->view(), "monitor1").part.quantity.item == "flask");
	CHECK_FALSE(item(l->view(), "monitor1").part.watching);
	ok(*l, R"({"do": "set", "part": "monitor1", "what": "malachite", "measure": "mol", "compare": "below", "value": 0.01})");
	const lab::Part p = item(l->view(), "monitor1").part;
	CHECK(p.watching);
	CHECK(p.quantity == lab::Quantity{ "flask", "malachite", "mol" });
	// the same item again keeps it; another forgets it
	ok(*l, R"({"do": "sense", "part": "monitor1", "item": "flask"})");
	CHECK(item(l->view(), "monitor1").part.watching);
	ok(*l, R"({"do": "sense", "part": "monitor1", "item": "burner"})");
	CHECK_FALSE(item(l->view(), "monitor1").part.watching);
	CHECK(item(l->view(), "monitor1").part.quantity.what.empty());
	ok(*l, R"({"do": "unsense", "part": "monitor1"})");
	CHECK(item(l->view(), "monitor1").part.quantity.item.empty());
	// only a monitor senses, and only an item of the bench
	ok(*l, R"({"do": "part", "kind": "alarm", "at": [2, 1]})");
	CHECK(refused(*l, R"({"do": "sense", "part": "alarm1", "item": "flask"})").find("senses nothing") != std::string::npos);
	refused(*l, R"({"do": "sense", "part": "monitor1", "item": "nowhere"})");
	refused(*l, R"({"do": "sense", "part": "monitor1", "item": "alarm1"})");
	// a set that names an item senses it, as every session written before sensing does
	ok(*l, R"({"do": "set", "part": "monitor1", "item": "flask", "what": "celsius", "compare": "above", "value": 50})");
	CHECK(item(l->view(), "monitor1").part.quantity.item == "flask");
}

TEST_CASE("sensing: the guided lists offer only what the item has now, by kind (Stage 26)") {
	auto l = demo();
	ok(*l, R"({"do": "tip", "species": "malachite", "from": "jar", "to": "flask", "all": true})");
	const lab::LabView v = l->view();
	const lab::ItemView &flask = item(v, "flask"), &burner = item(v, "burner"), &balloon = item(v, "balloon");
	// a material: what it holds, never its wall; a solid's particles too
	const std::vector<std::string> held = lab::materials_of(flask);
	CHECK(std::find(held.begin(), held.end(), "malachite") != held.end());
	CHECK(std::find(held.begin(), held.end(), "copper_sulfate_aq") == held.end());
	for (const lab::SubstanceView &s : flask.contents) {
		CHECK((s.wall ? std::find(held.begin(), held.end(), s.species) == held.end() : true));
	}
	CHECK(lab::measures_of(flask, "malachite") == std::vector<std::string>{ "g", "mol", "ml", "purity", "count", "size" });
	// a material's volume (Stage 28): its own, from its density
	const double ml = *lab::measure(v, lab::Quantity{ "flask", "malachite", "ml" }), g = *lab::measure(v, lab::Quantity{ "flask", "malachite", "g" });
	CHECK(ml > 0.0);
	CHECK(g / ml == doctest::Approx(4.0).epsilon(0.05)); // malachite: about 4 g/mL
	CHECK(*lab::measure(v, lab::Quantity{ "flask", "kpa", "g" }) == doctest::Approx(101.325)); // open: the room's pressure
	// the vessel's own quantities, in order; other: a burner's valve, a balloon's fill
	CHECK(lab::quantities_of_kind(flask, lab::QuantityKind::Vessel) == std::vector<std::string>{ "celsius", "kpa", "mass_g", "liquid_ml", "solid_ml", "condensed_ml", "gas_ml" });
	CHECK(lab::kinds_of(burner) == std::vector<lab::QuantityKind>{ lab::QuantityKind::Other });
	CHECK(lab::quantities_of_kind(burner, lab::QuantityKind::Other) == std::vector<std::string>{ "valve" });
	CHECK(lab::quantities_of_kind(balloon, lab::QuantityKind::Other) == std::vector<std::string>{ "fill" });
	// the new quantities read what the item holds
	const double total = *lab::measure(v, lab::Quantity{ "flask", "mass_g", "g" });
	double sum = 0.0;
	for (const lab::SubstanceView &s : flask.contents) {
		sum += s.wall ? 0.0 : s.g;
	}
	CHECK(total == doctest::Approx(sum));
	CHECK(total > 4.9);
	CHECK(*lab::measure(v, lab::Quantity{ "flask", "solid_ml", "g" }) == doctest::Approx(flask.solid_ml));
	CHECK(flask.solid_ml > 0.0); // the malachite
	// a solution holds no solid: the acid's sulfuric acid is dissolved (its volume the solution's, not a heap's)
	CHECK(item(v, "acid").solid_ml == 0.0);
	CHECK(item(v, "acid").condensed_ml > item(v, "acid").liquid_ml);
	CHECK(*lab::measure(v, lab::Quantity{ "flask", "malachite", "mol" }) == doctest::Approx(5.0 / 221.116).epsilon(1e-3));
	CHECK(lab::label(v, lab::Quantity{ "flask", "malachite", "mol" }).find("amount (mol)") != std::string::npos);
	CHECK(lab::label(v, lab::Quantity{ "flask", "mass_g", "g" }) == "flask: total mass (g)");
}
