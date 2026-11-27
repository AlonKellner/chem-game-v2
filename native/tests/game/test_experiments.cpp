// The tutorial's strict and free modes (GAME_SPEC §13.2) and the experiments
// (§13.7; Stage 41): the catalogue, its list on the screen, and each
// experiment played through the screen with nothing but its own gestures.
#include "lab/notebook.hpp"
#include "lab/optics_scene.hpp"
#include "lab/prism_demo.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "pupil.hpp"

#include <optional>
#include <string>

using lab::TutorialView;
using pupil::Pupil;
using pupil::read;

namespace {

// the first body's rectangle, a scene's signature for the prism's moving
std::string scene_signature(const lab::OpticsScene &scene) {
	const float *r = &scene.table[static_cast<size_t>(lab::optics_layout::HEADER_VECS) * 4];
	return std::to_string(r[0]) + "," + std::to_string(r[1]) + "," + std::to_string(r[2]) + "," + std::to_string(r[3]);
}

// data/experiments.json with each bench and tutorial inlined, as a host hands it to the screen
std::string catalogue() {
	nlohmann::json doc = nlohmann::json::parse(read("data/experiments.json"));
	for (nlohmann::json &e : doc["experiments"]) {
		if (e.contains("scene")) {
			continue; // a scene of the optics alone (Stage 47): no bench to inline
		}
		e["bench"] = nlohmann::json::parse(read("data/" + e["bench"].get<std::string>()));
		if (!e["tutorial"].is_null()) {
			e["tutorial"] = nlohmann::json::parse(read("data/" + e["tutorial"].get<std::string>()));
		}
	}
	return doc.dump();
}

int guides(Pupil &p) {
	int n = 0;
	for (const lab::DrawCommand &c : p.ui->draw().commands) {
		n += (c.color & 0xffffff00u) == 0xffc83c00u ? 1 : 0;
	}
	return n;
}

} // namespace

TEST_CASE("tutorial: free, every gesture is the player's and so is the clock; it still points, passes and fails (Stage 41)") {
	Pupil p;
	CHECK(p.ui->strict()); // strict as opened
	CHECK(p.drawn("strict: on"));
	p.click("tutorial:strict");
	CHECK_FALSE(p.ui->strict());
	CHECK(p.drawn("strict: off (free)"));
	CHECK(p.drawn("Free: the clock is yours."));
	// what step 1 does not ask for is taken
	std::string before = p.ui->lab().digest();
	p.drag("item:burner", "square:19,12");
	CHECK(p.ui->message().empty());
	CHECK(p.item("burner").place == "bench");
	CHECK(p.tutorial().step == 0);
	// the clock runs while the step asks for a gesture
	const int64_t at = p.ui->lab().step();
	p.ui->frame(1.0);
	CHECK(p.ui->lab().step() == at + 60);
	// the pointer still points at what the step asks for
	CHECK(guides(p) >= 4);
	// a step passes by the state, whatever the order
	p.drag("item:flask", "square:11,12");
	CHECK(p.tutorial().step == 1);
	p.drag("item:burner", "item:flask");
	CHECK(p.tutorial().step == 2);
	// strict again: at once only the step's gestures, and the clock waits
	p.click("tutorial:strict");
	CHECK(p.ui->strict());
	before = p.ui->lab().digest();
	p.drag("item:acid", "square:7,12");
	CHECK(p.ui->message().find("Not now") != std::string::npos);
	CHECK(p.ui->lab().digest() == before);
	const int64_t stood = p.ui->lab().step();
	p.ui->frame(1.0);
	CHECK(p.ui->lab().step() == stood);
	// free once more: a failure still fails the run
	p.click("tutorial:strict");
	p.drag("item:jar", "square:4,13");
	p.drag("item:jar", "slot:4");
	INFO(p.ui->message());
	CHECK(p.tutorial().state == TutorialView::Failed);
	CHECK(p.tutorial().failure.find("Only the balloon") != std::string::npos);
	// and reset keeps the mode
	p.click("reset");
	CHECK(p.tutorial().state == TutorialView::Running);
	CHECK(p.tutorial().step == 0);
	CHECK_FALSE(p.ui->strict());
}

TEST_CASE("tutorial: the switch is the player's in the part editor too, and debug's step is taken free (Stage 41)") {
	Pupil p;
	p.ui->set_strict(false);
	// a part made off the steps, its editor open: the switch is still taken
	p.drag("item:flask", "square:11,12");
	p.drag("drawer:monitor", "square:1,1");
	p.drag("sense:monitor1", "item:flask");
	float x = 0, y = 0;
	REQUIRE(p.ui->locate("edit:cancel", x, y));
	p.click("tutorial:strict");
	CHECK(p.ui->strict());
	REQUIRE(p.ui->locate("edit:cancel", x, y)); // the editor stays open
	p.click("tutorial:strict");
	p.click("edit:cancel");
	// debug's single step, refused strict while the clock waits, is taken free
	const int64_t at = p.ui->lab().step();
	p.click("debug");
	p.click("debug:step");
	CHECK(p.ui->lab().step() == at + 1);
}

TEST_CASE("experiments: the catalogue's list on the screen opens each bench and its steps; reset starts it again (Stage 41)") {
	Pupil p; // the demo, as a host opens it
	std::string why;
	// the catalogue's paths, read as a host reads them
	REQUIRE_MESSAGE(p.ui->experiments(read("data/experiments.json"), why, [](const std::string &path) -> std::optional<std::string> { return read("data/" + path); }), why);
	CHECK(p.ui->experiment() == "demo"); // what is open is marked as shown
	CHECK(p.drawn("experiments: demo"));
	float x = 0, y = 0;
	CHECK_FALSE(p.ui->locate("experiment:vacuum", x, y)); // only while the list is open
	p.click("menu:experiments");
	CHECK(p.drawn("Boiling under a vacuum"));
	CHECK(p.drawn("Hot and cold dissolving"));
	// strict, the list is still the player's
	REQUIRE(p.ui->strict());
	p.click("experiment:vacuum");
	CHECK(p.ui->message().empty());
	CHECK(p.ui->experiment() == "vacuum");
	REQUIRE(p.ui->tutorial() != nullptr);
	CHECK(p.ui->tutorial()->title() == "Experiment: boiling under a vacuum");
	CHECK(p.item("flask").place == "bench");
	CHECK(p.supplied("stopper")); // (Stage 61: the cabinet is Supplies, no copy out)
	// played a little, then reset: the vacuum's bench and its first step again
	p.drag("item:burner", "item:flask");
	CHECK(p.tutorial().step == 1);
	p.click("reset");
	CHECK(p.ui->experiment() == "vacuum");
	CHECK(p.tutorial().step == 0);
	CHECK(p.supplied("burner")); // (Stage 61: reset, the burner a supply again)
	// the mode stays through another experiment
	p.click("tutorial:strict");
	p.click("menu:experiments");
	p.click("experiment:free_lab");
	CHECK(p.ui->experiment() == "free_lab");
	CHECK(p.ui->tutorial() == nullptr);
	CHECK_FALSE(p.ui->strict());
	CHECK_FALSE(p.ui->locate("tutorial:strict", x, y));
	// a press beside the list closes it and does nothing else
	p.click("menu:experiments");
	const std::string before = p.ui->lab().digest();
	p.ui->pointer(300, 1000, false);
	p.ui->pointer(300, 1000, true);
	p.ui->pointer(300, 1000, false);
	CHECK_FALSE(p.ui->locate("experiment:demo", x, y));
	CHECK(p.ui->lab().digest() == before);
	REQUIRE(p.ui->choose("rate", why));
	CHECK(p.ui->experiment() == "rate");
	CHECK_FALSE(p.ui->choose("nothing", why));
	CHECK(why.find("nothing") != std::string::npos);
}

TEST_CASE("experiments: a catalogue with one that cannot be opened is refused whole (Stage 41)") {
	Pupil p;
	std::string why;
	nlohmann::json doc = nlohmann::json::parse(catalogue());
	CHECK_FALSE(p.ui->experiments("{}", why));
	CHECK_FALSE(p.ui->experiments(read("data/experiments.json"), why)); // paths, and no reader
	CHECK(why.find("a path a reader is given for") != std::string::npos);
	nlohmann::json bad = doc;
	bad["experiments"][1]["tutorial"]["steps"][0]["checks"][0]["when"]["item"] = "kettle";
	CHECK_FALSE(p.ui->experiments(bad.dump(), why));
	CHECK(why.find("experiment vacuum") != std::string::npos);
	bad = doc;
	bad["experiments"][2]["id"] = "demo";
	CHECK_FALSE(p.ui->experiments(bad.dump(), why));
	CHECK(p.ui->experiment().empty()); // none was taken
	float x = 0, y = 0;
	CHECK_FALSE(p.ui->locate("menu:experiments", x, y));
}

// ---- each experiment, played through to its end: strict with its own gestures at once, strict as a person
// plays it, and free with its own gestures

namespace {

void completed(Pupil &p) {
	REQUIRE(p.tutorial().state == TutorialView::Complete);
	CHECK(p.ui->lab().view().conserved);
	CHECK(p.drawn("Tutorial complete"));
	for (const std::string &r : p.tutorial().results) {
		MESSAGE(r);
	}
}

} // namespace

TEST_CASE("experiment vacuum: followed with its own gestures, strict, the flask falls below 10 kPa, boils at 50 C and pops its stopper (Stage 41)") {
	Pupil p("data/benches/vacuum.json", "data/tutorials/vacuum.json");
	CHECK(pupil::follow_through(p, 7200) > 15); // (Stage 53: its glass cools by its own outside, slower)
	completed(p);
	CHECK(p.item("stopper").place != "on");
	REQUIRE(p.tutorial().results.size() == 4);
	CHECK(p.tutorial().results[0].find("the flask's pressure: least ") == 0);
}

TEST_CASE("experiment vacuum: played as a person plays it, strict, it completes (Stage 41)") {
	Pupil p("data/benches/vacuum.json", "data/tutorials/vacuum.json");
	pupil::play_as_person(p, 7200);
	completed(p);
}

TEST_CASE("experiment vacuum: followed with its own gestures, free, it completes (Stage 41)") {
	Pupil p("data/benches/vacuum.json", "data/tutorials/vacuum.json", false);
	pupil::follow_through(p, 7200);
	completed(p);
}

TEST_CASE("experiment solution_heat: followed with its own gestures, strict, the white salt warms its water and the blue one cools its own (Stage 41)") {
	Pupil p("data/benches/solution_heat.json", "data/tutorials/solution_heat.json");
	CHECK(pupil::follow_through(p, 1800) > 15);
	completed(p);
	REQUIRE(p.tutorial().results.size() == 3);
	CHECK(p.tutorial().results[0].find("the left beaker (white salt): start 20.0 C, most ") == 0);
	CHECK(p.grams("warm", "copper_sulfate_aq") > 7.9);
	CHECK(p.grams("cool", "copper_sulfate_aq") > 7.9);
}

TEST_CASE("experiment solution_heat: played as a person plays it, strict, it completes (Stage 41)") {
	Pupil p("data/benches/solution_heat.json", "data/tutorials/solution_heat.json");
	pupil::play_as_person(p, 1800);
	completed(p);
}

TEST_CASE("experiment solution_heat: followed with its own gestures, free, it completes (Stage 41)") {
	Pupil p("data/benches/solution_heat.json", "data/tutorials/solution_heat.json", false);
	pupil::follow_through(p, 1800);
	completed(p);
}

TEST_CASE("experiment rate: followed with its own gestures, strict, the warm oxide is gone while the still and the stirred are not (Stage 41)") {
	Pupil p("data/benches/rate.json", "data/tutorials/rate.json");
	CHECK(pupil::follow_through(p, 3600) > 15);
	completed(p);
	CHECK(p.grams("still", "copper_oxide") > 0.9);
	CHECK(p.grams("stirred", "copper_oxide") > 0.9);
	CHECK(std::abs(p.grams("still", "copper_oxide") - p.grams("stirred", "copper_oxide")) < 0.01); // stirring hardly matters here
	CHECK(p.grams("hot", "copper_oxide") < 0.01);
}

TEST_CASE("experiment rate: played as a person plays it, strict, it completes (Stage 41)") {
	Pupil p("data/benches/rate.json", "data/tutorials/rate.json");
	pupil::play_as_person(p, 3600);
	completed(p);
}

TEST_CASE("experiment rate: followed with its own gestures, free, it completes (Stage 41)") {
	Pupil p("data/benches/rate.json", "data/tutorials/rate.json", false);
	pupil::follow_through(p, 3600);
	completed(p);
}

// ---- acids and bases (Stage 43)

TEST_CASE("experiment dilution: followed with its own gestures, strict, each tenfold dilution raises the pH by nearly one (Stage 43)") {
	Pupil p("data/benches/dilution.json", "data/tutorials/dilution.json");
	CHECK(pupil::follow_through(p, 600) > 10);
	completed(p);
	const auto ph = [&](const std::string &id) {
		REQUIRE(p.item(id).ph);
		return *p.item(id).ph;
	};
	CHECK(ph("hundredth") - ph("tenth") > 0.7);
	CHECK(ph("hundredth") - ph("tenth") < 1.0);
	CHECK(ph("thousandth") - ph("hundredth") > 0.8);
	CHECK(ph("thousandth") - ph("hundredth") < 1.0);
	CHECK(p.drawn(", pH ")); // the readout
}

TEST_CASE("experiment dilution: played as a person plays it, strict, it completes (Stage 43)") {
	Pupil p("data/benches/dilution.json", "data/tutorials/dilution.json");
	pupil::play_as_person(p, 600);
	completed(p);
}

TEST_CASE("experiment dilution: followed with its own gestures, free, it completes (Stage 43)") {
	Pupil p("data/benches/dilution.json", "data/tutorials/dilution.json", false);
	pupil::follow_through(p, 600);
	completed(p);
}

TEST_CASE("experiment neutralisation: followed with its own gestures, strict, the acid warms and its pH leaps past 7 (Stage 43)") {
	Pupil p("data/benches/neutralisation.json", "data/tutorials/neutralisation.json");
	CHECK(pupil::follow_through(p, 600) > 10);
	completed(p);
	REQUIRE(p.item("beaker").ph);
	CHECK(*p.item("beaker").ph > 11.0);
	CHECK(p.grams("beaker", "sodium_sulfate_aq") > 3.5); // 0.025 mol of it: 3.55 g
	CHECK(p.grams("beaker", "sulfuric_acid_aq") < 1e-6);
}

TEST_CASE("experiment neutralisation: played as a person plays it, strict, it completes (Stage 43)") {
	Pupil p("data/benches/neutralisation.json", "data/tutorials/neutralisation.json");
	pupil::play_as_person(p, 600);
	completed(p);
}

TEST_CASE("experiment neutralisation: followed with its own gestures, free, it completes (Stage 43)") {
	Pupil p("data/benches/neutralisation.json", "data/tutorials/neutralisation.json", false);
	pupil::follow_through(p, 600);
	completed(p);
}

// ---- precipitation and gas (Stage 44)

TEST_CASE("experiment precipitate: followed with its own gestures, strict, the copper comes out, turns black and dissolves blue again (Stage 44)") {
	Pupil p("data/benches/precipitate.json", "data/tutorials/precipitate.json");
	CHECK(pupil::follow_through(p, 2400) > 10);
	completed(p);
	CHECK(p.grams("beaker", "copper_hydroxide") < 1e-6);
	CHECK(p.grams("beaker", "copper_oxide") < 0.02);
	CHECK(p.grams("beaker", "copper_sulfate_aq") > 1.55); // 0.01 mol: 1.596 g, the cycle closed
}

TEST_CASE("experiment precipitate: played as a person plays it, strict, it completes (Stage 44)") {
	Pupil p("data/benches/precipitate.json", "data/tutorials/precipitate.json");
	pupil::play_as_person(p, 2400);
	completed(p);
}

TEST_CASE("experiment precipitate: followed with its own gestures, free, it completes (Stage 44)") {
	Pupil p("data/benches/precipitate.json", "data/tutorials/precipitate.json", false);
	pupil::follow_through(p, 2400);
	completed(p);
}

TEST_CASE("experiment fizz: followed with its own gestures, strict, the malachite fizzes away into the balloon (Stage 44)") {
	Pupil p("data/benches/fizz.json", "data/tutorials/fizz.json");
	CHECK(pupil::follow_through(p, 600) == 3);
	completed(p);
	CHECK(p.grams("flask", "malachite") < 0.01);
	CHECK(p.grams("balloon", "carbon_dioxide") > 0.05);
	CHECK(p.grams("flask", "copper_sulfate_aq") > 2.8); // 0.018 mol: 2.89 g
}

TEST_CASE("experiment fizz: played as a person plays it, strict, it completes (Stage 44)") {
	Pupil p("data/benches/fizz.json", "data/tutorials/fizz.json");
	pupil::play_as_person(p, 600);
	completed(p);
}

TEST_CASE("experiment fizz: followed with its own gestures, free, it completes (Stage 44)") {
	Pupil p("data/benches/fizz.json", "data/tutorials/fizz.json", false);
	pupil::follow_through(p, 600);
	completed(p);
}

// ---- boiling's bubbles (Stage 45)

TEST_CASE("experiment boiling: followed with its own gestures, strict, the bubbles climb from the floor and reach the surface at the boil (Stage 45)") {
	Pupil p("data/benches/boiling.json", "data/tutorials/boiling.json");
	CHECK(pupil::follow_through(p, 1800) > 8);
	completed(p);
	// a little longer, to its boiling point: the bubbles reach the surface
	std::string why;
	for (int s = 0; s < 600 && !p.item("beaker").bubbles_surface; ++s) {
		REQUIRE_MESSAGE(p.ui->lab().command(R"({"do": "wait", "seconds": 1})", why), why);
	}
	p.click("item:beaker"); // chosen: its readout
	p.frames(2);
	const lab::ItemView beaker = p.item("beaker");
	CHECK(beaker.boiling);
	CHECK(beaker.bubbles_surface);
	CHECK(beaker.bubbles_cm == doctest::Approx(beaker.depth_cm));
	CHECK(p.drawn("boiling: bubbles")); // the readout
}

TEST_CASE("experiment boiling: played as a person plays it, strict, it completes (Stage 45)") {
	Pupil p("data/benches/boiling.json", "data/tutorials/boiling.json");
	pupil::play_as_person(p, 1800);
	completed(p);
}

TEST_CASE("experiment boiling: followed with its own gestures, free, it completes (Stage 45)") {
	Pupil p("data/benches/boiling.json", "data/tutorials/boiling.json", false);
	pupil::follow_through(p, 1800);
	completed(p);
}

// ---- a scene of the optics alone in the list (Stage 47)

TEST_CASE("experiments: the prism and the grating are in the list; chosen, the screen shows that scene, its prism dragged, and back to a bench (Stage 47)") {
	Pupil p; // the demo, as a host opens it
	std::string why;
	REQUIRE_MESSAGE(p.ui->experiments(read("data/experiments.json"), why, [](const std::string &path) -> std::optional<std::string> { return read("data/" + path); }), why);
	p.click("menu:experiments");
	CHECK(p.drawn("Light through a prism and a grating"));
	p.click("experiment:prism");
	CHECK(p.ui->message().empty());
	CHECK(p.ui->experiment() == "prism");
	p.ui->frame(1.0 / 60.0);
	CHECK(p.drawn("experiments: prism")); // the button stays over the scene
	const lab::OpticsScene &scene = p.ui->optics();
	CHECK(scene.table[0] >= 2.0f); // the prism and the grating are bodies of the pass
	CHECK(lab::check_O1(scene) == "");
	// the prism dragged: the scene's own gesture
	const lab::PrismDemo probe;
	const float x0 = probe.centre_x() + 1, y0 = probe.centre_y();
	const std::string before = scene_signature(p.ui->optics());
	p.ui->pointer(x0, y0, false);
	p.ui->pointer(x0, y0, true);
	p.ui->pointer(x0 + 120, y0, true);
	p.ui->pointer(x0 + 120, y0, false);
	CHECK(scene_signature(p.ui->optics()) != before);
	// back to a bench, from the list over the scene
	p.click("menu:experiments");
	p.click("experiment:free_lab");
	CHECK(p.ui->experiment() == "free_lab");
	CHECK(p.drawn("supplies")); // (Stage 61: the cabinet panel is named Supplies)
	// a catalogue naming a scene there is not is refused
	CHECK_FALSE(p.ui->experiments(R"({"format": "chem-experiments/1", "experiments": [{"id": "x", "title": "X", "scene": "kaleidoscope"}]})", why, nullptr));
	CHECK(why.find("scene") != std::string::npos);
}

TEST_CASE("experiments: the prism chosen after the lab is its dark room on the screen, every table's revision a new one, and the lab after it its own again (Stage 47)") {
	// the user's test: the prism's scene showed the lab's white wall and its grid. A host uploads a table only when
	// its revision changes, and the prism's scene counted its revisions from 0 as the lab's had.
	Pupil p;
	std::string why;
	REQUIRE_MESSAGE(p.ui->experiments(read("data/experiments.json"), why, [](const std::string &path) -> std::optional<std::string> { return read("data/" + path); }), why);
	const auto revisions = [&] {
		p.ui->frame(1.0 / 60.0);
		const lab::OpticsScene &s = p.ui->optics();
		return std::array<uint64_t, 4>{ s.revision, s.wall_revision, s.shapes_revision, s.images_revision };
	};
	const auto luminance = [&](float x, float y) {
		double rgb[3];
		lab::optics_truth(p.ui->optics(), static_cast<int>(x), static_cast<int>(y), rgb);
		return 0.2126729 * rgb[0] + 0.7151522 * rgb[1] + 0.0721750 * rgb[2];
	};
	const std::array<uint64_t, 4> lab_seen = revisions();
	const float wall_x = 700.0f, wall_y = 300.0f; // the lab's bench, its wall; the prism's dark room, far from its slit
	CHECK(luminance(wall_x, wall_y) > 0.3);
	p.click("menu:experiments");
	p.click("experiment:prism");
	REQUIRE(p.ui->experiment() == "prism");
	const std::array<uint64_t, 4> prism_seen = revisions();
	for (size_t k = 0; k < 3; ++k) {
		CHECK(prism_seen[k] != lab_seen[k]); // (the images: neither has any)
	}
	CHECK(luminance(wall_x, wall_y) < 0.01);
	p.click("menu:experiments");
	p.click("experiment:free_lab");
	const std::array<uint64_t, 4> back = revisions();
	for (size_t k = 0; k < 3; ++k) {
		CHECK(back[k] != prism_seen[k]);
	}
	CHECK(luminance(wall_x, wall_y) > 0.3);
}

TEST_CASE("experiment hydrate: followed with its own gestures, strict, the crystals lose their water and the balance gives x of 5 (Stage 50)") {
	Pupil p("data/benches/hydrate.json", "data/tutorials/hydrate.json");
	CHECK(pupil::follow_through(p, 3600) > 9);
	completed(p);
	REQUIRE(p.tutorial().results.size() == 4);
	// every copper is in the dish or the jar, as the salt or a hydrate (the jar's crystals lose a little water to the
	// room's dry air: GAME_SPEC §20)
	const auto copper = [&](const std::string &id) {
		return p.grams(id, "copper_sulfate") / 159.609 + p.grams(id, "copper_sulfate_monohydrate") / 177.624 + p.grams(id, "copper_sulfate_trihydrate") / 213.655 +
				p.grams(id, "copper_sulfate_pentahydrate") / 249.685;
	};
	CHECK(std::fabs(copper("dish") + copper("jar") - 10.0 / 249.685) < 1e-6); // (a micromole: the tables' molar masses against these)
	// what was taken: the crystals of the dish's copper
	const double taken = copper("dish") * 249.685;
	CHECK(taken > 1.8);
	CHECK(taken < 2.2);
	// the balance, tared on the dish, reads its solids, less the air they displace
	double solids = 0.0;
	for (const lab::SubstanceView &s : p.item("dish").contents) {
		solids += s.phase == 's' && !s.wall ? s.g : 0.0;
	}
	const double reads = *p.item("scale").display_g;
	CHECK(std::fabs(reads - solids) < 0.002);
	// what the student works out from the balance: 5, to the balance's readability and the crystals' few mg lost in the jar
	const double x = (taken - reads) / 18.015 / (reads / 159.609);
	MESSAGE("x from the balance: " << x);
	CHECK(x > 4.95);
	CHECK(x < 5.05);
}

TEST_CASE("experiment hydrate: played as a person plays it, strict, it completes (Stage 50)") {
	Pupil p("data/benches/hydrate.json", "data/tutorials/hydrate.json");
	pupil::play_as_person(p, 3600);
	completed(p);
}

TEST_CASE("experiment hydrate: followed with its own gestures, free, it completes (Stage 50)") {
	Pupil p("data/benches/hydrate.json", "data/tutorials/hydrate.json", false);
	pupil::follow_through(p, 3600);
	completed(p);
}

TEST_CASE("experiment carbonate: followed with its own gestures, strict, the malachite keeps 72 % of its mass as black oxide (Stage 50)") {
	Pupil p("data/benches/carbonate.json", "data/tutorials/carbonate.json");
	CHECK(pupil::follow_through(p, 3600) > 6);
	completed(p);
	const double taken = 10.0 - p.grams("jar", "malachite"), oxide = p.grams("dish", "copper_oxide");
	CHECK(std::fabs(oxide / taken - 2.0 * 79.545 / 221.116) < 0.002);
	CHECK(taken > 1.35);
	CHECK(taken < 1.65);
}

TEST_CASE("experiment carbonate: played as a person plays it, strict, it completes (Stage 50)") {
	Pupil p("data/benches/carbonate.json", "data/tutorials/carbonate.json");
	pupil::play_as_person(p, 3600);
	completed(p);
}

TEST_CASE("experiment carbonate: followed with its own gestures, free, it completes (Stage 50)") {
	Pupil p("data/benches/carbonate.json", "data/tutorials/carbonate.json", false);
	pupil::follow_through(p, 3600);
	completed(p);
}

TEST_CASE("experiment standard: followed with its own gestures, strict, the flask holds what was weighed in its own volume, and the second a quarter of it (Stage 52)") {
	Pupil p("data/benches/standard.json", "data/tutorials/standard.json");
	CHECK(pupil::follow_through(p, 3600) > 7);
	completed(p);
	const double first = p.ui->lab().view().items.empty() ? 0.0 : p.grams("flask", "copper_sulfate_aq"), second = p.grams("diluted", "copper_sulfate_aq");
	// a quarter of the first's, by the pipette's true volume over the first flask's
	const double share = *p.item("pipette").mark_ml / (*p.item("flask").mark_ml + 0.0);
	CHECK(second / (first + second) == doctest::Approx(share).epsilon(2e-3));
	CHECK(std::fabs(p.item("flask").condensed_ml - 100.0) < 0.1 + 25.1);
}

TEST_CASE("experiment standard: played as a person plays it, strict, it completes (Stage 52)") {
	Pupil p("data/benches/standard.json", "data/tutorials/standard.json");
	pupil::play_as_person(p, 3600);
	completed(p);
}

TEST_CASE("experiment standard: followed with its own gestures, free, it completes (Stage 52)") {
	Pupil p("data/benches/standard.json", "data/tutorials/standard.json", false);
	pupil::follow_through(p, 3600);
	completed(p);
}

TEST_CASE("experiment titration: followed with its own gestures, strict, the base run in gives the acid's concentration (Stage 52)") {
	Pupil p("data/benches/titration.json", "data/tutorials/titration.json");
	CHECK(pupil::follow_through(p, 7200) > 8);
	completed(p);
	// the end point: the base run in neutralised the acid pipetted, within the last drops' overshoot
	const double acid_mol = p.grams("flask", "sodium_sulfate_aq") / 142.04; // all of it neutralised, as sodium sulfate
	const double base_ml = p.item("flask").condensed_ml - *p.item("pipette").mark_ml;
	MESSAGE("base run in " << base_ml << " mL for " << acid_mol << " mol of acid");
	CHECK(*p.item("flask").ph > 7.0);
	CHECK(*p.item("flask").ph < 11.0); // not far past it
	CHECK(base_ml > 25.0);
	CHECK(base_ml < 27.5);
}

TEST_CASE("experiment titration: played as a person plays it, strict, it completes (Stage 52)") {
	Pupil p("data/benches/titration.json", "data/tutorials/titration.json");
	pupil::play_as_person(p, 7200);
	completed(p);
	CHECK(*p.item("flask").ph < 11.0);
}

TEST_CASE("experiment titration: followed with its own gestures, free, it completes (Stage 52)") {
	Pupil p("data/benches/titration.json", "data/tutorials/titration.json", false);
	pupil::follow_through(p, 7200);
	completed(p);
}

TEST_CASE("experiment calorimeter: followed with its own gestures, strict, the cup warms by about 8 K and its thermometer follows (Stage 52)") {
	Pupil p("data/benches/calorimeter.json", "data/tutorials/calorimeter.json");
	CHECK(pupil::follow_through(p, 3600) > 4);
	completed(p);
	CHECK(p.grams("cup", "sodium_sulfate_aq") / 142.04 == doctest::Approx(0.025).epsilon(0.15)); // 25 mL of the 1 M acid, by hand
}

TEST_CASE("experiment calorimeter: played as a person plays it, strict, it completes (Stage 52)") {
	Pupil p("data/benches/calorimeter.json", "data/tutorials/calorimeter.json");
	pupil::play_as_person(p, 3600);
	completed(p);
}

TEST_CASE("experiment calorimeter: followed with its own gestures, free, it completes (Stage 52)") {
	Pupil p("data/benches/calorimeter.json", "data/tutorials/calorimeter.json", false);
	pupil::follow_through(p, 3600);
	completed(p);
}

namespace {

// the report filled in: each item's truth worked out, typed (a little off), and checked on the card
void report(Pupil &p, const std::vector<double> &typed) {
	for (size_t k = 0; k < typed.size(); ++k) {
		p.click("report:" + std::to_string(k));
		for (int b = 0; b < 20; ++b) {
			p.ui->key(lab::Ui::KeyBackspace); // what was typed before, cleared
		}
		char number[32];
		std::snprintf(number, sizeof number, "%.6g", typed[k]);
		p.type(number);
	}
	p.click("report:check");
	p.ui->frame(1.0 / 60.0);
}

double truth_of(Pupil &p, const std::string &expression) {
	double v = 0.0;
	std::string why;
	REQUIRE_MESSAGE(lab::evaluate(expression, p.ui->lab().view(), v, why), why);
	return v;
}

} // namespace

TEST_CASE("experiment titration_curve: followed with its own gestures, strict, the notebook logs the curve and the report checks its end point (Stage 54)") {
	Pupil p("data/benches/titration_curve.json", "data/tutorials/titration_curve.json");
	CHECK(pupil::follow_through(p, 7200) > 9);
	completed(p);
	// the notebook: pH and the burette's reading at the same moments, a dozen and more across the leap
	const lab::LabView v = p.ui->lab().view();
	int ph = 0, ml = 0, paired = 0;
	for (const lab::NoteEntry &e : v.notebook) {
		ph += e.quantity.what == "ph" ? 1 : 0;
		ml += e.quantity.what == "scale_ml" ? 1 : 0;
		for (const lab::NoteEntry &f : v.notebook) {
			paired += e.quantity.what == "ph" && f.quantity.what == "scale_ml" && f.step == e.step ? 1 : 0;
		}
	}
	MESSAGE(ph << " pH readings, " << ml << " burette readings, " << paired << " paired");
	CHECK(paired > 20);
	CHECK(paired >= std::min(ph, ml) - 1); // (the first of each written by hand, at its own moment)
	// the report: the truth typed, within; a wrong end point, off
	const double end = truth_of(p, p.ui->tutorial()->report()[0].truth), c = truth_of(p, p.ui->tutorial()->report()[1].truth);
	CHECK(end == doctest::Approx(2 * 25.0 * c / 0.0997).epsilon(0.01)); // 25 mL of the acid, the 0.0997 M base
	report(p, { end + 0.1, c * 1.01 });
	CHECK(p.drawn("yours is within it"));
	report(p, { end + 3.0, c * 1.2 });
	CHECK(p.drawn("yours is off by"));
}

TEST_CASE("experiment titration_curve: played as a person plays it, strict, it completes (Stage 54)") {
	Pupil p("data/benches/titration_curve.json", "data/tutorials/titration_curve.json");
	pupil::play_as_person(p, 7200);
	completed(p);
}

TEST_CASE("experiment titration_curve: followed with its own gestures, free, it completes (Stage 54)") {
	Pupil p("data/benches/titration_curve.json", "data/tutorials/titration_curve.json", false);
	pupil::follow_through(p, 7200);
	completed(p);
}

TEST_CASE("experiment vapour: followed with its own gestures, strict, ln p against 1/T gives water's heat of vaporisation (Stage 54)") {
	Pupil p("data/benches/vapour.json", "data/tutorials/vapour.json");
	CHECK(pupil::follow_through(p, 7200) > 6);
	completed(p);
	// Clausius and Clapeyron from what the notebook holds, by the instruments' readings
	const lab::LabView v = p.ui->lab().view();
	std::vector<double> x, y;
	for (const lab::NoteEntry &t : v.notebook) {
		for (const lab::NoteEntry &k : v.notebook) {
			if (t.quantity.what == "celsius" && k.quantity.what == "kpa" && t.step == k.step) {
				x.push_back(*lab::transformed(t.value, lab::AxisTransform::Inverse, true));
				y.push_back(*lab::transformed(k.value, lab::AxisTransform::Log, false));
			}
		}
	}
	const lab::LineFit f = lab::fit_line(x, y);
	REQUIRE(f.ok);
	const double dh = -f.slope * 8.314462618 / 1000.0;
	MESSAGE(x.size() << " points, dH " << dh << " kJ/mol, R2 " << f.r2);
	CHECK(f.r2 > 0.99);
	CHECK(dh == doctest::Approx(41.93).epsilon(0.06));
	report(p, { dh });
	CHECK(p.drawn("yours is within it"));
}

TEST_CASE("experiment vapour: strict, the valve is shut before the stopper goes in, and stays shut while it cools (Stage 54)") {
	// the user's report: the stopper put in first passed the step, and the valve could not be shut after it
	Pupil p("data/benches/vapour.json", "data/tutorials/vapour.json");
	const int steps = p.tutorial().steps;
	while (p.tutorial().state == lab::TutorialView::Running && p.tutorial().title != "Off" && p.tutorial().step < steps) {
		const int step = p.tutorial().step;
		for (const lab::TutorialGesture &g : p.ui->tutorial()->gestures()) {
			p.play(g);
		}
		p.watch([&] { return p.tutorial().step != step; }, 7200);
	}
	REQUIRE(p.tutorial().title == "Off");
	p.drag("item:stopper", "item:flask"); // the stopper first: not yet
	CHECK(p.supplied("stopper")); // (Stage 61: not on the flask: refused, no copy is taken out of Supplies)
	CHECK(p.ui->message().find("Not now") != std::string::npos);
	p.click("item:burner");
	p.click("valve:burner@0");
	CHECK(p.item("burner").valve == 0.0);
	CHECK(p.tutorial().title == "Stoppered");
	pupil::follow_through(p, 7200);
	completed(p);
	CHECK(p.item("burner").valve == 0.0);
}

TEST_CASE("experiment vapour: played as a person plays it, strict, it completes (Stage 54)") {
	Pupil p("data/benches/vapour.json", "data/tutorials/vapour.json");
	pupil::play_as_person(p, 7200);
	completed(p);
}

TEST_CASE("experiment vapour: followed with its own gestures, free, it completes (Stage 54)") {
	Pupil p("data/benches/vapour.json", "data/tutorials/vapour.json", false);
	pupil::follow_through(p, 7200);
	completed(p);
}

TEST_CASE("experiment beer: followed with its own gestures, strict, the standards' line and the unknown on it, beside a prediction (Stage 55)") {
	Pupil p("data/benches/beer.json", "data/tutorials/beer.json");
	std::string why;
	REQUIRE_MESSAGE(p.ui->experiments(read("data/experiments.json"), why, [](const std::string &path) -> std::optional<std::string> { return read("data/" + path); }), why);
	REQUIRE_MESSAGE(p.ui->choose("beer", why), why); // from the list: its progress is the list's
	CHECK(p.ui->progress().find("\"beer\"") == std::string::npos);
	CHECK(pupil::follow_through(p, 600) > 30);
	completed(p);
	// the calibration line from the notebook, paired as the plot pairs them: c written, then A recorded
	const lab::LabView v = p.ui->lab().view();
	std::vector<double> c, a;
	double unknown_a = 0.0, last_c = -1.0;
	for (const lab::NoteEntry &e : v.notebook) {
		if (e.label == "c") {
			last_c = e.value;
		} else if (e.quantity.what == "absorbance") {
			if (c.size() < 4) {
				c.push_back(last_c);
				a.push_back(e.value);
			} else {
				unknown_a = e.value;
			}
		}
	}
	REQUIRE(c.size() == 4);
	const lab::LineFit f = lab::fit_line(c, a);
	REQUIRE(f.ok);
	const double found = (unknown_a - f.intercept) / f.slope, truth = truth_of(p, p.ui->tutorial()->report()[1].truth);
	MESSAGE("slope " << f.slope << " L/mol, intercept " << f.intercept << ", R2 " << f.r2 << "; the unknown " << found << " mol/L against " << truth);
	CHECK(f.r2 > 0.999);
	CHECK(f.slope == doctest::Approx(10.23).epsilon(0.04));
	CHECK(found == doctest::Approx(truth).epsilon(0.03));
	report(p, { f.slope, found });
	CHECK(p.drawn("yours is within it"));
	CHECK(p.drawn("you predicted 0.05"));
	// progress: done, marked in the list; the next lesson is another
	CHECK(p.ui->progress().find("\"beer\"") != std::string::npos);
	p.click("menu:experiments");
	CHECK(p.drawn("done"));
	CHECK(p.drawn("next"));
}

TEST_CASE("experiment beer: played as a person plays it, strict, it completes (Stage 55)") {
	Pupil p("data/benches/beer.json", "data/tutorials/beer.json");
	pupil::play_as_person(p, 600);
	completed(p);
}

TEST_CASE("experiment beer: followed with its own gestures, free, it completes (Stage 55)") {
	Pupil p("data/benches/beer.json", "data/tutorials/beer.json", false);
	pupil::follow_through(p, 600);
	completed(p);
}

TEST_CASE("experiments: progress is kept as given back, refused when it is not one, and marks the next lesson (Stage 55)") {
	Pupil p;
	std::string why;
	REQUIRE_MESSAGE(p.ui->experiments(read("data/experiments.json"), why, [](const std::string &path) -> std::optional<std::string> { return read("data/" + path); }), why);
	CHECK(p.ui->progress() == R"({"done":[],"format":"chem-progress/1"})");
	REQUIRE_MESSAGE(p.ui->set_progress(R"({"format": "chem-progress/1", "done": ["demo", "vacuum", "gone"]})", why), why);
	CHECK(p.ui->progress() == R"({"done":["demo","gone","vacuum"],"format":"chem-progress/1"})"); // an id another catalogue may have, kept
	CHECK_FALSE(p.ui->set_progress("{}", why));
	CHECK_FALSE(p.ui->set_progress(R"({"format": "chem-progress/1", "done": [1]})", why));
	CHECK(p.ui->progress().find("vacuum") != std::string::npos); // a refusal changes nothing
	p.click("menu:experiments");
	CHECK(p.drawn("done"));
	CHECK(p.drawn("next")); // solution_heat, the first not done
}

namespace {

// the rates' runs from the notebook, as a student reads them: each run's start and end, its temperature, its time
struct Run {
	double celsius = 0.0, seconds = 0.0;
};
std::vector<Run> rate_runs(const lab::LabView &v) {
	std::vector<Run> runs;
	std::vector<int64_t> marks;
	double celsius = 0.0;
	for (const lab::NoteEntry &e : v.notebook) {
		if (e.quantity.what == "absorbance") {
			marks.push_back(e.step);
		} else if (e.quantity.what == "celsius") {
			celsius = e.value;
			runs.push_back(Run{ celsius, static_cast<double>(marks.back() - marks[marks.size() - 2]) / 60.0 });
		}
	}
	return runs;
}

} // namespace

TEST_CASE("experiment rates: followed with its own gestures, strict, ln t against 1/T gives the activation energy, half the acid its order (Stage 56)") {
	Pupil p("data/benches/rates.json", "data/tutorials/rates.json");
	CHECK(pupil::follow_through(p, 1800) > 40);
	completed(p);
	const std::vector<Run> runs = rate_runs(p.ui->lab().view());
	REQUIRE(runs.size() == 4);
	std::vector<double> x, y;
	for (size_t k = 0; k < 3; ++k) {
		x.push_back(1.0 / (runs[k].celsius + 273.15));
		y.push_back(std::log(runs[k].seconds));
		MESSAGE(runs[k].celsius << " C: " << runs[k].seconds << " s");
	}
	MESSAGE("half the acid at " << runs[3].celsius << " C: " << runs[3].seconds << " s");
	const lab::LineFit f = lab::fit_line(x, y);
	REQUIRE(f.ok);
	const double ea = f.slope * 8.314462618 / 1000.0, order = std::log(runs[3].seconds / runs[1].seconds) / std::log(2.0);
	MESSAGE("Ea " << ea << " kJ/mol (R2 " << f.r2 << "), order " << order);
	CHECK(ea == doctest::Approx(69.0).epsilon(0.15));
	CHECK(order == doctest::Approx(0.5).epsilon(0.3));
	report(p, { ea, order });
	CHECK(p.drawn("yours is within it"));
	CHECK(p.drawn("you predicted 1"));
}

TEST_CASE("experiment rates: played as a person plays it, strict, it completes (Stage 56)") {
	Pupil p("data/benches/rates.json", "data/tutorials/rates.json");
	pupil::play_as_person(p, 1800);
	completed(p);
}

TEST_CASE("experiment rates: followed with its own gestures, free, it completes (Stage 56)") {
	Pupil p("data/benches/rates.json", "data/tutorials/rates.json", false);
	pupil::follow_through(p, 1800);
	completed(p);
}

TEST_CASE("experiment solubility: followed with its own gestures, strict, the curve and its heat of solution (Stage 57)") {
	Pupil p("data/benches/solubility.json", "data/tutorials/solubility.json");
	CHECK(pupil::follow_through(p, 3600) > 30);
	completed(p);
	// the points as the plot pairs them: a temperature recorded, then s written
	std::vector<double> x, y, celsius, s;
	double t = 0.0;
	for (const lab::NoteEntry &e : p.ui->lab().view().notebook) {
		if (e.quantity.what == "celsius") {
			t = e.value;
		} else if (e.label == "s") {
			celsius.push_back(t);
			s.push_back(e.value);
			x.push_back(1.0 / (t + 273.15));
			y.push_back(std::log(e.value));
		}
	}
	REQUIRE(x.size() == 4);
	for (size_t k = 0; k < 4; ++k) {
		MESSAGE(s[k] << " g per 100 g: its last crystal gone at " << celsius[k] << " C");
	}
	const lab::LineFit f = lab::fit_line(x, y);
	REQUIRE(f.ok);
	const double dh = -f.slope * 8.314462618 / 1000.0, at50 = std::exp(f.intercept + f.slope / 323.15);
	MESSAGE("dH " << dh << " kJ/mol (R2 " << f.r2 << "), " << at50 << " g at 50 C");
	CHECK(dh == doctest::Approx(15.0).epsilon(0.25));
	CHECK(at50 == doctest::Approx(34.6).epsilon(0.08));
	report(p, { at50, dh });
	CHECK(p.drawn("yours is within it"));
	CHECK(p.drawn("you predicted 30"));
}

TEST_CASE("experiment solubility: played as a person plays it, strict, it completes (Stage 57)") {
	Pupil p("data/benches/solubility.json", "data/tutorials/solubility.json");
	pupil::play_as_person(p, 3600);
	completed(p);
}

TEST_CASE("experiment solubility: followed with its own gestures, free, it completes (Stage 57)") {
	Pupil p("data/benches/solubility.json", "data/tutorials/solubility.json", false);
	pupil::follow_through(p, 3600);
	completed(p);
}

TEST_CASE("experiment prepare: followed with its own gestures, strict, the calculator plans it and the balance's reading gives what was made (Stage 58)") {
	Pupil p("data/benches/prepare.json", "data/tutorials/prepare.json");
	CHECK(pupil::follow_through(p, 1800) > 20);
	completed(p);
	CHECK(p.drawn("n = m / M")); // the calculator shows its sum
	double reading = 0.0, planned = 0.0;
	for (const lab::NoteEntry &e : p.ui->lab().view().notebook) {
		reading = e.quantity.what == "reads_g" ? e.value : reading;
		planned = e.label == "m" ? e.value : planned;
	}
	CHECK(planned == doctest::Approx(1.2484).epsilon(1e-3));
	const double made = reading / 249.68 / 0.100, truth = truth_of(p, p.ui->tutorial()->report()[0].truth);
	MESSAGE("planned " << planned << " g, weighed " << reading << " g: " << made << " mol/L against " << truth);
	CHECK(made == doctest::Approx(truth).epsilon(0.005));
	report(p, { made });
	CHECK(p.drawn("yours is within it"));
}

TEST_CASE("experiment prepare: played as a person plays it, strict, it completes (Stage 58)") {
	Pupil p("data/benches/prepare.json", "data/tutorials/prepare.json");
	pupil::play_as_person(p, 1800);
	completed(p);
}

TEST_CASE("experiment prepare: followed with its own gestures, free, it completes (Stage 58)") {
	Pupil p("data/benches/prepare.json", "data/tutorials/prepare.json", false);
	pupil::follow_through(p, 1800);
	completed(p);
}

TEST_CASE("experiment separate: followed with its own gestures, strict, the oxide filtered off and weighed (Stage 59)") {
	Pupil p("data/benches/separate.json", "data/tutorials/separate.json");
	CHECK(pupil::follow_through(p, 1800) > 15);
	completed(p);
	double reading = 0.0;
	for (const lab::NoteEntry &e : p.ui->lab().view().notebook) {
		reading = e.quantity.what == "reads_g" ? e.value : reading;
	}
	const double truth = truth_of(p, p.ui->tutorial()->report()[0].truth);
	MESSAGE("weighed " << reading << " g of the oxide against " << truth << " g in the mixture; in the dish " << p.grams("dish", "copper_oxide") << " g, through the paper "
					   << p.grams("flask", "copper_oxide") << " g; the filtrate's copper sulfate " << p.grams("flask", "copper_sulfate_aq") << " g");
	CHECK(p.grams("flask", "copper_oxide") == 0.0);
	CHECK(reading == doctest::Approx(truth).epsilon(0.005));
	report(p, { reading });
	CHECK(p.drawn("yours is within it"));
	CHECK(p.drawn("you predicted 1"));
}

TEST_CASE("experiment separate: played as a person plays it, strict, it completes (Stage 59)") {
	Pupil p("data/benches/separate.json", "data/tutorials/separate.json");
	pupil::play_as_person(p, 1800);
	completed(p);
}

TEST_CASE("experiment separate: followed with its own gestures, free, it completes (Stage 59)") {
	Pupil p("data/benches/separate.json", "data/tutorials/separate.json", false);
	pupil::follow_through(p, 1800);
	completed(p);
}

TEST_CASE("experiment yield: followed with its own gestures, strict, planned, precipitated, filtered and weighed: the percent yield (Stage 60)") {
	Pupil p("data/benches/yield.json", "data/tutorials/yield.json");
	CHECK(pupil::follow_through(p, 1800) > 30);
	completed(p);
	std::vector<double> readings;
	for (const lab::NoteEntry &e : p.ui->lab().view().notebook) {
		if (e.quantity.what == "reads_g") {
			readings.push_back(e.value);
		}
	}
	REQUIRE(readings.size() == 2);
	const double theoretical = readings[0] / 249.68 * 97.56, found = 100.0 * readings[1] / theoretical;
	const double truth = truth_of(p, p.ui->tutorial()->report()[0].truth);
	MESSAGE("crystals " << readings[0] << " g, hydroxide " << readings[1] << " g of a theoretical " << theoretical << ": " << found << " % against " << truth << " %");
	CHECK(found == doctest::Approx(truth).epsilon(0.005));
	report(p, { found });
	CHECK(p.drawn("yours is within it"));
	CHECK(p.drawn("you predicted 90"));
}

TEST_CASE("experiment yield: played as a person plays it, strict, it completes (Stage 60)") {
	Pupil p("data/benches/yield.json", "data/tutorials/yield.json");
	pupil::play_as_person(p, 1800);
	completed(p);
}

TEST_CASE("experiment yield: followed with its own gestures, free, it completes (Stage 60)") {
	Pupil p("data/benches/yield.json", "data/tutorials/yield.json", false);
	pupil::follow_through(p, 1800);
	completed(p);
}

TEST_CASE("experiment quota: followed with its own gestures, strict, planned from 3.00 g, made and handed in against the plan's goal (Stage 62)") {
	Pupil p("data/benches/quota.json", "data/tutorials/quota.json");
	CHECK(pupil::follow_through(p, 7200) > 30);
	completed(p);
	const lab::LabView v = p.ui->lab().view();
	const lab::Goal *goal = nullptr;
	for (const lab::Goal &g : v.goals) {
		goal = g.slot == 1 ? &g : goal;
	}
	REQUIRE(goal != nullptr);
	CHECK(goal->name == "3.00 g of CuSO4.5H2O");
	double malachite = 0.0;
	for (const lab::NoteEntry &e : v.notebook) {
		malachite = e.quantity.what == "reads_g" ? e.value : malachite;
	}
	const double crop = p.grams("dish", "copper_sulfate_pentahydrate"), share = truth_of(p, p.ui->tutorial()->report()[1].truth);
	MESSAGE("malachite " << malachite << " g (the plan's 1.90); the crop " << crop << " g, " << share << " % of the copper");
	CHECK(malachite == doctest::Approx(1.90).epsilon(0.12)); // a hand's within 10 %
	CHECK(crop > 2.4);
	CHECK(p.grams("flask", "copper_sulfate_pentahydrate") == 0.0); // the paper keeps the crystals
	report(p, { crop, share });
	CHECK(p.drawn("yours is within it"));
	CHECK(p.drawn("you predicted 3"));
}

TEST_CASE("experiment quota: played as a person plays it, strict, it completes (Stage 62)") {
	Pupil p("data/benches/quota.json", "data/tutorials/quota.json");
	pupil::play_as_person(p, 7200);
	completed(p);
}

TEST_CASE("experiment quota: followed with its own gestures, free, it completes (Stage 62)") {
	Pupil p("data/benches/quota.json", "data/tutorials/quota.json", false);
	pupil::follow_through(p, 7200);
	completed(p);
}

namespace {

// a solid's crystals in an item, as the lab's view counts them
int64_t crystals_in(Pupil &p, const std::string &item, const std::string &species) {
	for (const lab::ItemView &v : p.ui->lab().view().items) {
		if (v.id == item) {
			for (const lab::SubstanceView &s : v.contents) {
				if (s.species == species) {
					return s.particles;
				}
			}
		}
	}
	return 0;
}

} // namespace

TEST_CASE("experiment seed: followed with its own gestures, strict, the stirred seed breeds and sets the excess out, the still one only grows (Stage 66)") {
	Pupil p("data/benches/seed.json", "data/tutorials/seed.json");
	CHECK(pupil::follow_through(p, 3600) > 5);
	completed(p);
	const double still = p.grams("still", "copper_sulfate_pentahydrate"), stirred = p.grams("stirred", "copper_sulfate_pentahydrate");
	MESSAGE("still: " << still * 1e3 << " mg in " << crystals_in(p, "still", "copper_sulfate_pentahydrate") << "; stirred: " << stirred << " g in "
					  << crystals_in(p, "stirred", "copper_sulfate_pentahydrate"));
	CHECK(crystals_in(p, "still", "copper_sulfate_pentahydrate") == 1);
	CHECK(still < 0.02);
	CHECK(crystals_in(p, "stirred", "copper_sulfate_pentahydrate") > 100000);
	CHECK(stirred == doctest::Approx(1.05).epsilon(0.03)); // the excess at 20 C, its waters with it, on the seed
	report(p, { stirred });
	CHECK(p.drawn("yours is within it"));
	CHECK(p.drawn("you predicted 0.1"));
}

TEST_CASE("experiment seed: played as a person plays it, strict, it completes (Stage 66)") {
	Pupil p("data/benches/seed.json", "data/tutorials/seed.json");
	pupil::play_as_person(p, 3600);
	completed(p);
}

TEST_CASE("experiment seed: followed with its own gestures, free, it completes (Stage 66)") {
	Pupil p("data/benches/seed.json", "data/tutorials/seed.json", false);
	pupil::follow_through(p, 3600);
	completed(p);
}

TEST_CASE("experiment standardise: followed with its own gestures, strict, the KHP weighed and the base run in to the pink give the base's concentration (Stage 65)") {
	Pupil p("data/benches/standardise.json", "data/tutorials/standardise.json");
	CHECK(pupil::follow_through(p, 3600) > 10);
	completed(p);
	// what a student works out: the balance's reading over 204.22 g/mol, in the base the burette's readings ran in
	const lab::LabView v = p.ui->lab().view();
	double mass = 0.0, first = 0.0, last = 0.0;
	int burette = 0;
	for (const lab::NoteEntry &e : v.notebook) {
		if (e.quantity.what == "reads_g") {
			mass = e.value;
		}
		if (e.quantity.what == "scale_ml") {
			(burette++ == 0 ? first : last) = e.value;
		}
	}
	const double found = mass / 204.22 / ((last - first) * 1e-3), truth = truth_of(p, p.ui->tutorial()->report()[0].truth);
	MESSAGE(mass << " g of KHP, " << last - first << " mL of base: " << found << " mol/L against " << truth);
	CHECK(std::abs(found / truth - 1.0) < 0.005);
	report(p, { found });
	CHECK(p.drawn("yours is within it"));
	report(p, { truth * 1.05 });
	CHECK(p.drawn("yours is off by"));
}

TEST_CASE("experiment standardise: played as a person plays it, strict, it completes (Stage 65)") {
	Pupil p("data/benches/standardise.json", "data/tutorials/standardise.json");
	pupil::play_as_person(p, 3600);
	completed(p);
}

TEST_CASE("experiment standardise: followed with its own gestures, free, it completes (Stage 65)") {
	Pupil p("data/benches/standardise.json", "data/tutorials/standardise.json", false);
	pupil::follow_through(p, 3600);
	completed(p);
}

TEST_CASE("experiment vinegar: followed with its own gestures, strict, the diluted vinegar's curve gives its end point, its strength and the acid's pKa (Stage 65)") {
	Pupil p("data/benches/vinegar.json", "data/tutorials/vinegar.json");
	CHECK(pupil::follow_through(p, 7200) > 10);
	completed(p);
	// the aliquot a tenth of what was diluted: the volumetric flask mixed before it was drawn
	const auto acid = [&](const char *id) { return p.grams(id, "acetic_acid_aq") / 60.052 + p.grams(id, "acetate_ion") / 59.044; };
	const double in_flask = acid("flask"), left = acid("dilute");
	MESSAGE("the aliquot " << in_flask << " mol of the " << in_flask + left << " diluted: " << in_flask / (in_flask + left));
	CHECK(in_flask / (in_flask + left) == doctest::Approx(0.1).epsilon(0.01));
	// the curve: the end point its steepest step, the pKa the pH at half of it
	const lab::LabView v = p.ui->lab().view();
	std::vector<std::pair<double, double>> curve; // (mL, pH) at the same moments
	for (const lab::NoteEntry &e : v.notebook) {
		if (e.quantity.what != "ph") {
			continue;
		}
		for (const lab::NoteEntry &f : v.notebook) {
			if (f.quantity.what == "scale_ml" && f.step == e.step) {
				curve.emplace_back(f.value, e.value);
			}
		}
	}
	REQUIRE(curve.size() > 20);
	double steepest = 0.0, end = 0.0;
	for (size_t k = 1; k < curve.size(); ++k) {
		const double dx = curve[k].first - curve[k - 1].first;
		if (dx > 0.0 && (curve[k].second - curve[k - 1].second) / dx > steepest) {
			steepest = (curve[k].second - curve[k - 1].second) / dx;
			end = 0.5 * (curve[k].first + curve[k - 1].first);
		}
	}
	const double start = curve.front().first, half = start + 0.5 * (end - start);
	double pka = 0.0;
	for (size_t k = 1; k < curve.size(); ++k) {
		if (curve[k - 1].first <= half && curve[k].first >= half) {
			pka = curve[k - 1].second + (curve[k].second - curve[k - 1].second) * (half - curve[k - 1].first) / std::max(curve[k].first - curve[k - 1].first, 1e-9);
		}
	}
	const double truth_end = truth_of(p, p.ui->tutorial()->report()[0].truth), truth_c = truth_of(p, p.ui->tutorial()->report()[1].truth);
	const double c = 0.100 * (end - start) / 25.0 * 10.0;
	MESSAGE("end point " << end - start << " mL (truth " << truth_end << "), vinegar " << c << " mol/L (truth " << truth_c << "), pKa read " << pka);
	CHECK(end - start == doctest::Approx(truth_end).epsilon(0.02));
	CHECK(std::abs(c / truth_c - 1.0) < 0.015);
	CHECK(pka == doctest::Approx(4.68).epsilon(0.03));
	report(p, { end - start, c, pka });
	CHECK(p.drawn("yours is within it"));
}

TEST_CASE("experiment vinegar: played as a person plays it, strict, it completes (Stage 65)") {
	Pupil p("data/benches/vinegar.json", "data/tutorials/vinegar.json");
	pupil::play_as_person(p, 7200);
	completed(p);
}

TEST_CASE("experiment vinegar: followed with its own gestures, free, it completes (Stage 65)") {
	Pupil p("data/benches/vinegar.json", "data/tutorials/vinegar.json", false);
	pupil::follow_through(p, 7200);
	completed(p);
}

namespace {

// a logged curve: (the burette's reading, the pH) at the same moments
std::vector<std::pair<double, double>> logged_curve(Pupil &p) {
	const lab::LabView v = p.ui->lab().view();
	std::vector<std::pair<double, double>> curve;
	for (const lab::NoteEntry &e : v.notebook) {
		if (e.quantity.what != "ph") {
			continue;
		}
		for (const lab::NoteEntry &f : v.notebook) {
			if (f.quantity.what == "scale_ml" && f.step == e.step) {
				curve.emplace_back(f.value, e.value);
			}
		}
	}
	return curve;
}

// where the curve falls (or rises) steepest with its pH between `lo` and `hi`, mL past its start: its slope over
// `window` mL, as a step's middle is read from a plot (a gentle step's single intervals tie within a reading's 0.01)
double steepest_between(const std::vector<std::pair<double, double>> &curve, double lo, double hi, double window = 0.6) {
	double best = 0.0, at = 0.0;
	for (size_t i = 0; i < curve.size(); ++i) {
		size_t j = i;
		while (j + 1 < curve.size() && curve[j].first - curve[i].first < window) {
			++j;
		}
		const double dx = curve[j].first - curve[i].first, mid = 0.5 * (curve[i].second + curve[j].second);
		if (dx >= 0.5 * window && mid > lo && mid < hi && std::abs(curve[j].second - curve[i].second) / dx > best) {
			best = std::abs(curve[j].second - curve[i].second) / dx;
			at = 0.5 * (curve[i].first + curve[j].first);
		}
	}
	return at - curve.front().first;
}

} // namespace

TEST_CASE("experiment carbonate_curve: followed with its own gestures, strict, the curve's two steps, the second twice the first (Stage 65)") {
	Pupil p("data/benches/carbonate_curve.json", "data/tutorials/carbonate_curve.json");
	CHECK(pupil::follow_through(p, 7200) > 9);
	completed(p);
	const std::vector<std::pair<double, double>> curve = logged_curve(p);
	REQUIRE(curve.size() > 20);
	const double first = steepest_between(curve, 6.5, 10.0), second = steepest_between(curve, 3.0, 6.5);
	const double t1 = truth_of(p, p.ui->tutorial()->report()[0].truth), t2 = truth_of(p, p.ui->tutorial()->report()[1].truth), tc = truth_of(p, p.ui->tutorial()->report()[2].truth);
	const double c = 0.100 * first / 25.0;
	MESSAGE("first end point " << first << " mL (truth " << t1 << "), second " << second << " (truth " << t2 << "), carbonate " << c << " mol/L (truth " << tc << ")");
	CHECK(std::abs(first - t1) < 0.4);
	CHECK(std::abs(second - t2) < 0.5);
	CHECK(std::abs(c / tc - 1.0) < 0.03);
	report(p, { first, second, c });
	CHECK(p.drawn("yours is within it"));
}

TEST_CASE("experiment carbonate_curve: played as a person plays it, strict, it completes (Stage 65)") {
	Pupil p("data/benches/carbonate_curve.json", "data/tutorials/carbonate_curve.json");
	pupil::play_as_person(p, 7200);
	completed(p);
}

TEST_CASE("experiment carbonate_curve: followed with its own gestures, free, it completes (Stage 65)") {
	Pupil p("data/benches/carbonate_curve.json", "data/tutorials/carbonate_curve.json", false);
	pupil::follow_through(p, 7200);
	completed(p);
}

TEST_CASE("experiment buffer: followed with its own gestures, strict, the buffer's pH is Henderson and Hasselbalch's less its activity, and it holds where water does not (Stage 65)") {
	Pupil p("data/benches/buffer.json", "data/tutorials/buffer.json");
	CHECK(pupil::follow_through(p, 3600) > 10);
	completed(p);
	const double made = truth_of(p, "buffer.ph"), a = truth_of(p, "a.ph"), b = truth_of(p, "b.ph");
	const double hh = truth_of(p, "4.76 + log10(buffer.sodium_ion.mol / (buffer.acetic_acid_aq.mol + buffer.acetate_ion.mol - buffer.sodium_ion.mol))");
	MESSAGE("Henderson and Hasselbalch " << hh << ", the buffer " << made << "; after the acid, the buffer " << a << ", the water " << b);
	CHECK(made == doctest::Approx(hh - 0.11).epsilon(0.02)); // the ions' activity at 0.1 M
	CHECK(made - a > 0.03);
	CHECK(made - a < 0.15);
	CHECK(b < 2.5);
	report(p, { made, a, b });
	CHECK(p.drawn("yours is within it"));
	CHECK(p.drawn("you predicted 4.76"));
}

TEST_CASE("experiment buffer: played as a person plays it, strict, it completes (Stage 65)") {
	Pupil p("data/benches/buffer.json", "data/tutorials/buffer.json");
	pupil::play_as_person(p, 3600);
	completed(p);
}

TEST_CASE("experiment buffer: followed with its own gestures, free, it completes (Stage 65)") {
	Pupil p("data/benches/buffer.json", "data/tutorials/buffer.json", false);
	pupil::follow_through(p, 3600);
	completed(p);
}
