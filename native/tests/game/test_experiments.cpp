// The tutorial's strict and free modes (GAME_SPEC §13.2) and the experiments
// (§13.7; Stage 41): the catalogue, its list on the screen, and each
// experiment played through the screen with nothing but its own gestures.
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
	CHECK(p.item("stopper").place == "cabinet");
	// played a little, then reset: the vacuum's bench and its first step again
	p.drag("item:burner", "item:flask");
	CHECK(p.tutorial().step == 1);
	p.click("reset");
	CHECK(p.ui->experiment() == "vacuum");
	CHECK(p.tutorial().step == 0);
	CHECK(p.item("burner").place == "cabinet");
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
	CHECK(pupil::follow_through(p, 3600) > 15);
	completed(p);
	CHECK(p.item("stopper").place != "on");
	REQUIRE(p.tutorial().results.size() == 4);
	CHECK(p.tutorial().results[0].find("the flask's pressure: least ") == 0);
}

TEST_CASE("experiment vacuum: played as a person plays it, strict, it completes (Stage 41)") {
	Pupil p("data/benches/vacuum.json", "data/tutorials/vacuum.json");
	pupil::play_as_person(p, 3600);
	completed(p);
}

TEST_CASE("experiment vacuum: followed with its own gestures, free, it completes (Stage 41)") {
	Pupil p("data/benches/vacuum.json", "data/tutorials/vacuum.json", false);
	pupil::follow_through(p, 3600);
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
	CHECK(p.drawn("cabinet"));
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
