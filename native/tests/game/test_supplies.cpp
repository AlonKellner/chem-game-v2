// Supplies and Storage (GAME_SPEC §13, Stage 61): what a bench gives without a square is a supply, a template no
// part of the room's world, of which each copy taken out joins it (the ledger counting what came in: C9); Storage, the
// outbox, holds what is put aside, still simulated, in as many slots as it needs.
#include "lab/lab.hpp"
#include "lab/search.hpp"
#include "lab/optics_scene.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string read(const std::string &relative) {
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/" + relative);
	REQUIRE_MESSAGE(static_cast<bool>(in), relative);
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

std::unique_ptr<lab::Lab> bench(const char *file = "data/benches/lab.json") {
	std::string why;
	auto made = lab::Lab::open(read(file), why);
	REQUIRE_MESSAGE(made != nullptr, why);
	return made;
}

void must(lab::Lab &l, const std::string &command) {
	std::string why;
	REQUIRE_MESSAGE(l.command(command, why), command << ": " << why);
}

const lab::ItemView *find(const lab::LabView &v, const std::string &id) {
	for (const lab::ItemView &x : v.items) {
		if (x.id == id) {
			return &x;
		}
	}
	return nullptr;
}

} // namespace

TEST_CASE("supplies: what a bench gives without a square is a supply, no part of the room's world (Stage 61)") {
	auto l = bench();
	const lab::LabView v = l->view();
	REQUIRE(v.supplies.size() >= 8); // the free lab: all of it supplies
	for (const lab::ItemView &s : v.supplies) {
		CHECK(s.place == "supplies");
		CHECK(find(v, s.id) == nullptr); // (no item of it yet)
	}
	CHECK(v.conserved);
	// as it is when taken out: the flask's view holds its air at the room's temperature
	const lab::ItemView *flask = nullptr;
	for (const lab::ItemView &s : v.supplies) {
		flask = s.id == "flask" ? &s : flask;
	}
	REQUIRE(flask != nullptr);
	CHECK(flask->celsius == doctest::Approx(v.room_celsius).epsilon(1e-6));
}

TEST_CASE("supplies: a copy taken out joins the world at the room's temperature; another is a new item; the world conserves across its boundary (C9, Stage 61)") {
	auto l = bench();
	l->set_checking(true);
	must(*l, R"({"do": "take", "supply": "beaker", "at": [10, 4]})");
	CHECK(l->last_taken() == "beaker");
	must(*l, R"({"do": "take", "supply": "beaker"})");
	CHECK(l->last_taken() == "beaker2");
	lab::LabView v = l->view();
	REQUIRE(find(v, "beaker") != nullptr);
	REQUIRE(find(v, "beaker2") != nullptr);
	CHECK(find(v, "beaker")->place == "bench");
	CHECK(find(v, "beaker2")->place == "outbox"); // (no square: in Storage)
	int copies = 0;
	for (const int n : v.copies) {
		copies += n;
	}
	CHECK(copies == 2);
	CHECK(v.conserved);
	std::string why;
	REQUIRE_MESSAGE(l->advance(600, why), why);
	CHECK(l->view().conserved);
	CHECK(find(l->view(), "beaker")->celsius == doctest::Approx(20.0).epsilon(1e-3));
}

TEST_CASE("supplies: a command naming a supply none of whose copies is out takes one out first, to where it puts it (Stage 61)") {
	auto l = bench();
	l->set_checking(true);
	must(*l, R"({"do": "place", "item": "flask", "at": [10, 4]})"); // (the cabinet's place, as before)
	must(*l, R"({"do": "heat", "burner": "burner", "vessel": "flask"})");
	must(*l, R"({"do": "attach", "item": "balloon", "to": "flask"})");
	const lab::LabView v = l->view();
	CHECK(find(v, "flask")->place == "bench");
	CHECK(find(v, "burner")->place == "under");
	CHECK(find(v, "balloon")->place == "on");
	CHECK(v.conserved);
	std::string why;
	REQUIRE_MESSAGE(l->advance(60 * 10, why), why);
	CHECK(l->view().conserved);
}

TEST_CASE("storage: an item put aside is in Storage, still simulated, and comes back to the bench (Stage 61)") {
	auto l = bench("data/benches/demo.json");
	must(*l, R"({"do": "heat", "burner": "burner", "vessel": "flask"})");
	must(*l, R"({"do": "valve", "burner": "burner", "to": 1})");
	std::string why;
	REQUIRE_MESSAGE(l->advance(60 * 60, why), why);
	must(*l, R"({"do": "unheat", "burner": "burner"})");
	const double hot = find(l->view(), "flask")->celsius;
	REQUIRE(hot > 40.0);
	must(*l, R"({"do": "stow", "item": "flask"})");
	CHECK(find(l->view(), "flask")->place == "outbox");
	REQUIRE_MESSAGE(l->advance(60 * 300, why), why);
	CHECK(find(l->view(), "flask")->celsius < hot - 5.0); // (it cools there)
	must(*l, R"({"do": "place", "item": "flask", "at": [9, 8]})");
	CHECK(find(l->view(), "flask")->place == "bench");
}

TEST_CASE("trash: an item in it is frozen as it went in, restored exactly as it was, and thaws (Stage 61)") {
	auto l = bench("data/benches/demo.json");
	l->set_checking(true);
	must(*l, R"({"do": "heat", "burner": "burner", "vessel": "flask"})");
	must(*l, R"({"do": "valve", "burner": "burner", "to": 1})");
	std::string why;
	REQUIRE_MESSAGE(l->advance(60 * 60, why), why);
	must(*l, R"({"do": "unheat", "burner": "burner"})");
	must(*l, R"({"do": "trash", "item": "flask"})");
	const lab::ItemView in = *find(l->view(), "flask");
	REQUIRE(in.celsius > 40.0);
	REQUIRE_MESSAGE(l->advance(60 * 600, why), why); // ten minutes
	const lab::ItemView later = *find(l->view(), "flask");
	CHECK(later.celsius == in.celsius); // not a hair of it changed
	CHECK(later.wall_c == in.wall_c);
	CHECK(l->view().conserved);
	must(*l, R"({"do": "restore", "item": "flask"})");
	CHECK(find(l->view(), "flask")->place == "bench");
	CHECK(find(l->view(), "flask")->celsius == doctest::Approx(in.celsius).epsilon(1e-6)); // (its state as it was; read open, not sealed)
	REQUIRE_MESSAGE(l->advance(60 * 300, why), why);
	CHECK(find(l->view(), "flask")->celsius < in.celsius - 2.0); // thawed, it cools
}

TEST_CASE("trash: emptied, its items leave the world (the ledger counts them: C9) and their ids are not given again (Stage 61)") {
	auto l = bench();
	l->set_checking(true);
	must(*l, R"({"do": "take", "supply": "beaker", "at": [10, 4]})");
	must(*l, R"({"do": "trash", "item": "beaker"})");
	const double mass = l->view().mass_g;
	must(*l, R"({"do": "empty_trash"})");
	const lab::LabView v = l->view();
	CHECK(v.conserved);
	CHECK(v.mass_g < mass - 50.0); // (the beaker's glass is gone)
	CHECK(find(v, "beaker")->place == "retired");
	std::string why;
	const bool placed = l->command(R"({"do": "place", "item": "beaker", "at": [10, 4]})", why); // (a new copy, not what left)
	CHECK((!placed || l->last_taken() != "beaker"));
	must(*l, R"({"do": "take", "supply": "beaker"})");
	CHECK(l->last_taken() != "beaker");
	REQUIRE_MESSAGE(l->advance(600, why), why);
	CHECK(l->view().conserved);
}

TEST_CASE("supplies: a lab with copies out, one frozen in the trash and one emptied out of the world, saved and loaded, goes on exactly (Stage 61)") {
	auto first = bench();
	first->set_checking(true);
	must(*first, R"({"do": "take", "supply": "beaker", "at": [10, 4]})");
	must(*first, R"({"do": "take", "supply": "flask", "at": [16, 4]})");
	must(*first, R"({"do": "take", "supply": "beaker"})");
	must(*first, R"({"do": "trash", "item": "beaker2"})");
	must(*first, R"({"do": "empty_trash"})");
	must(*first, R"({"do": "trash", "item": "flask"})");
	std::string why;
	REQUIRE_MESSAGE(first->advance(120, why), why);
	auto second = lab::Lab::load(first->save(), why);
	REQUIRE_MESSAGE(second != nullptr, why);
	CHECK(second->digest() == first->digest());
	CHECK(second->view().supplies.size() == first->view().supplies.size());
	REQUIRE_MESSAGE(first->advance(600, why), why);
	REQUIRE_MESSAGE(second->advance(600, why), why);
	CHECK(second->digest() == first->digest());
	must(*first, R"({"do": "take", "supply": "beaker"})");
	must(*second, R"({"do": "take", "supply": "beaker"})");
	CHECK(second->last_taken() == first->last_taken());
	CHECK(second->last_taken() == "beaker3"); // (beaker2 left the world: its id is not given again)
	CHECK(second->digest() == first->digest());
	must(*second, R"({"do": "restore", "item": "flask"})");
	CHECK(second->view().conserved);
}

namespace {

std::vector<std::string> found(const lab::LabView &v, const std::string &query) {
	std::vector<std::string> out;
	for (const lab::ItemView &s : v.supplies) {
		if (lab::search::matches(query, lab::search::keywords(s))) {
			out.push_back(s.id);
		}
	}
	return out;
}

} // namespace

TEST_CASE("search: a query's words, in any order and case, each start a keyword of what it is, holds, is made of or is like (Stage 61)") {
	auto l = bench();
	const lab::LabView v = l->view();
	// (Stage 64: and the free lab's reagents, the indicators and the salts in 50 mL beakers; Stage 76: the metals' salts,
	// limewater and chalk; Stage 81: the complexes' reagents, EDTA, the buffer and its indicator; Stage 82: the flame tests'
	// solutions)
	CHECK(found(v, "") == std::vector<std::string>{ "flask", "stopper", "balloon", "condenser", "beaker", "waste", "jar", "acid", "burner", "viewer", "camera", "scale",
			"hydrochloric", "hydroxide", "vinegar", "ammonia", "phenolphthalein", "bromothymol", "methyl_red", "khp", "soda", "bicarbonate", "silver_nitrate", "barium_chloride", "calcium_chloride", "magnesium_sulfate", "iron_iii_chloride", "iron_ii_sulfate", "zinc_sulfate", "aluminium_chloride", "cobalt_chloride",
			"thiocyanate", "chromate", "dichromate", "ammonia_strong", "hydrochloric_strong", "hydroxide_strong", "edta", "buffer_10", "eriochrome", "limewater", "chalk", "salt",
			"acetate", "ammonium", "sodium_chloride", "potassium_chloride", "lithium_chloride", "strontium_chloride", "copper_sulfate" });
	CHECK(found(v, "BEAKER") == std::vector<std::string>{ "beaker", "waste", "jar", "phenolphthalein", "bromothymol", "methyl_red", "khp", "soda", "bicarbonate", "eriochrome",
			"chalk", "salt", "acetate", "ammonium", "sodium_chloride", "potassium_chloride", "lithium_chloride", "strontium_chloride", "copper_sulfate" }); // (the waste and the jar are beakers; Stage 81: the indicator's; Stage 82: the flame tests' solutions)
	CHECK(found(v, "conical") == std::vector<std::string>{ "flask" });
	CHECK(found(v, "250 beaker") == std::vector<std::string>{ "beaker", "waste" });
	CHECK(found(v, "malach") == std::vector<std::string>{ "jar" });
	CHECK(found(v, "sulfuric") == std::vector<std::string>{ "acid" }); // (Stage 76: not a sulfate's trace of bisulfate)
	CHECK(found(v, "solution") == std::vector<std::string>{ "acid", "hydrochloric", "hydroxide", "vinegar", "ammonia", "phenolphthalein", "bromothymol", "methyl_red",
			"silver_nitrate", "barium_chloride", "calcium_chloride", "magnesium_sulfate", "iron_iii_chloride", "iron_ii_sulfate", "zinc_sulfate", "aluminium_chloride", "cobalt_chloride",
			"thiocyanate", "chromate", "dichromate", "ammonia_strong", "hydrochloric_strong", "hydroxide_strong", "edta", "buffer_10", "eriochrome", "limewater",
			"sodium_chloride", "potassium_chloride", "lithium_chloride", "strontium_chloride", "copper_sulfate" });
	CHECK(found(v, "latex") == std::vector<std::string>{ "stopper", "balloon" });
	CHECK(found(v, "empty glass") == std::vector<std::string>{ "flask", "condenser", "beaker", "waste" });
	CHECK(found(v, "camera") == std::vector<std::string>{ "camera" });
	CHECK(found(v, "balance") == std::vector<std::string>{ "scale" });
	CHECK(found(v, "hot").empty());
	CHECK(found(v, "flask malachite").empty()); // (every word, of one item)
	// its state: a beaker taken out and heated is hot; a goal's name finds what is handed in for it
	lab::ItemView hot = v.supplies[4];
	hot.celsius = 80.0;
	CHECK(lab::search::matches("hot beaker", lab::search::keywords(hot)));
	CHECK(lab::search::matches("water", lab::search::keywords(hot, "Pure water")));
}

namespace {

// a bench whose only supply is a beaker of water
const char *const ONE_SUPPLY = R"({"format": "chem-bench/2", "room": {"celsius": 20, "air_mol": 1000}, "items": [
	{"id": "beaker", "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "beaker", "wall_g": 80, "contents": {"water": {"g": 100}}}]})";

} // namespace

TEST_CASE("pictures: a supply's box is its picture, captured once of it alone, painted by the screen under the pass as the pass drew it there, and no body of the pass; given back, none is captured again (Stage 61)") {
	std::string why;
	auto ui = lab::Ui::open(ONE_SUPPLY, why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	ui->frame(1.0 / 60.0);
	// drawn live by the pass until it has its picture
	const lab::OpticsScene &live_scene = ui->optics();
	CHECK(lab::optics_body(live_scene, "supply:beaker") >= 0);
	std::vector<std::array<double, 4>> live;
	for (int y = 100; y < 196; y += 7) {
		for (int x = 12; x < 108; x += 7) {
			std::array<double, 4> rgba{};
			lab::optics_pixel(live_scene, x, y, rgba.data());
			live.push_back(rgba);
		}
	}
	CHECK_FALSE(ui->pictures_changed());
	ui->capture_pictures();
	CHECK(ui->pictures_changed());
	const std::string kept = ui->pictures();
	CHECK_FALSE(ui->pictures_changed());
	ui->frame(1.0 / 60.0);
	// (the user: a picture in the scene was seen, a screen's three primaries, through the rims of what was held)
	CHECK(lab::optics_body(ui->optics(), "supply:beaker") < 0);
	const lab::DrawList &list = ui->draw();
	const lab::DrawCommand *painted = nullptr;
	for (int k = 0; k < list.above; ++k) {
		painted = list.commands[static_cast<size_t>(k)].type == lab::DrawCommand::Image ? &list.commands[static_cast<size_t>(k)] : painted;
	}
	REQUIRE(painted != nullptr); // under the pass
	const lab::DrawImage &picture = list.images[static_cast<size_t>(painted->first)];
	REQUIRE(picture.rgb != nullptr);
	double worst = 0.0;
	size_t k = 0;
	for (int y = 100; y < 196; y += 7) {
		for (int x = 12; x < 108; x += 7, ++k) {
			if (live[k][3] < 0.999) {
				continue; // (where the pass drew nothing the picture holds the wall as painted)
			}
			const int px = static_cast<int>((static_cast<float>(x) - painted->x) * static_cast<float>(picture.w) / painted->w);
			const int py = static_cast<int>((static_cast<float>(y) - painted->y) * static_cast<float>(picture.h) / painted->h);
			for (int c = 0; c < 3; ++c) {
				const double shown = picture.rgb[(static_cast<size_t>(py) * static_cast<size_t>(picture.w) + static_cast<size_t>(px)) * 3 + static_cast<size_t>(c)] / 255.0;
				worst = std::max(worst, std::fabs(shown - live[k][static_cast<size_t>(c)]));
			}
		}
	}
	MESSAGE("the picture against the pass: worst " << worst);
	CHECK(worst < 1.5 / 255.0);
	// given back to a screen of the same bench, it is painted at once, and nothing is captured
	auto again = lab::Ui::open(ONE_SUPPLY, why);
	REQUIRE(again != nullptr);
	REQUIRE_MESSAGE(again->add_pictures(kept, why), why);
	again->capture_pictures(); // (it has every one: none to capture)
	CHECK_FALSE(again->pictures_changed());
	again->frame(1.0 / 60.0);
	CHECK(lab::optics_body(again->optics(), "supply:beaker") < 0);
}

namespace {

float body_width(lab::Ui &ui, const std::string &id) {
	const lab::OpticsScene &scene = ui.optics();
	const int b = lab::optics_body(scene, id);
	REQUIRE_MESSAGE(b >= 0, "no body " << id);
	return scene.table[static_cast<size_t>(lab::optics_layout::HEADER_VECS + b * lab::optics_layout::BODY_VECS) * 4 + 2];
}

} // namespace

TEST_CASE("supplies: a supply pressed is its copy in hand at its full size, simulated and drawn; put nowhere, the lab is as it was (Stage 61)") {
	std::string why;
	auto ui = lab::Ui::open(read("data/benches/lab.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	ui->frame(1.0 / 60.0);
	float x = 0, y = 0, sx = 0, sy = 0;
	REQUIRE(ui->locate("item:flask", x, y));
	REQUIRE(ui->locate("square:12,6", sx, sy));
	// pressed: a copy, in the world, in hand
	const std::string start = ui->lab().digest(); // (the lab as it is at the press: the clock runs between frames)
	ui->pointer(x, y, true);
	ui->pointer(x, y, false);
	CHECK(ui->lab().last_taken() == "flask");
	CHECK(ui->lab().digest() != start);
	ui->pointer(sx, sy, false);
	const float in_hand = body_width(*ui, "flask");
	{
		// (the user: it was drawn off the screen, held by its storage slot's place, not by where its box was pressed)
		const lab::OpticsScene &scene = ui->optics();
		const int b = lab::optics_body(scene, "flask");
		REQUIRE(b >= 0);
		const float *r = &scene.table[static_cast<size_t>(lab::optics_layout::HEADER_VECS + b * lab::optics_layout::BODY_VECS) * 4];
		CHECK(sx >= r[0]);
		CHECK(sx <= r[0] + r[2]);
		CHECK(sy >= r[1]);
		CHECK(sy <= r[1] + r[3]); // under the pointer
		double rgba[4];
		lab::optics_pixel(scene, static_cast<int>(r[0] + r[2] / 2), static_cast<int>(r[1] + r[3] / 2), rgba);
		CHECK(rgba[3] > 0.5); // and seen there
	}
	// Escape: nothing was taken
	ui->key(lab::Ui::KeyEscape, 0);
	CHECK(ui->lab().digest() == start);
	// pressed again and put on a square: it stays, as large as it was in hand
	REQUIRE(ui->locate("item:flask", x, y));
	ui->pointer(x, y, true);
	ui->pointer(x, y, false);
	ui->pointer(sx, sy, false);
	ui->pointer(sx, sy, true);
	ui->pointer(sx, sy, false);
	ui->frame(1.0 / 60.0);
	bool placed = false;
	for (const lab::ItemView &v : ui->lab().view().items) {
		placed = placed || (v.id == "flask" && v.place == "bench");
	}
	CHECK(placed);
	CHECK(body_width(*ui, "flask") == doctest::Approx(in_hand).epsilon(0.02));
	CHECK(in_hand > 60.0f); // (its box is 100 px: in hand it is the size it stands on the bench)
	// a second copy pressed and put back over Supplies: nothing more was taken
	const std::string placed_digest = ui->lab().digest();
	float cx = 0, cy = 0;
	REQUIRE(ui->locate("cabinet", cx, cy));
	REQUIRE(ui->locate("item:beaker", x, y));
	ui->pointer(x, y, true);
	ui->pointer(x, y, false);
	ui->pointer(cx, cy, false);
	ui->pointer(cx, cy, true);
	ui->pointer(cx, cy, false);
	CHECK(ui->lab().digest() == placed_digest);
}

namespace {

// the screen's colours over a held flask's middle
std::vector<double> held_colours(lab::Ui &ui) {
	const lab::OpticsScene &scene = ui.optics();
	const int b = lab::optics_body(scene, "flask");
	REQUIRE(b >= 0);
	const float *r = &scene.table[static_cast<size_t>(lab::optics_layout::HEADER_VECS + b * lab::optics_layout::BODY_VECS) * 4];
	std::vector<double> out;
	for (int y = static_cast<int>(r[1] + r[3] * 0.55f); y < static_cast<int>(r[1] + r[3] * 0.9f); y += 5) {
		for (int x = static_cast<int>(r[0] + r[2] * 0.3f); x < static_cast<int>(r[0] + r[2] * 0.7f); x += 5) {
			double rgba[4];
			lab::optics_pixel(scene, x, y, rgba);
			out.insert(out.end(), { rgba[0], rgba[1], rgba[2] });
		}
	}
	return out;
}

void type(lab::Ui &ui, const std::string &field, const std::string &text) {
	float x = 0, y = 0;
	REQUIRE(ui.locate(field, x, y));
	ui.pointer(x, y, true);
	ui.pointer(x, y, false);
	for (const char c : text) {
		ui.key(lab::Ui::KeyText, static_cast<uint32_t>(c));
	}
	ui.key(lab::Ui::KeyEnter, 0);
}

} // namespace

TEST_CASE("supplies: what is held over Supplies sees the room's wall through it, not the panel's boxes (Stage 61)") {
	std::string why;
	auto ui = lab::Ui::open(read("data/benches/lab.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	REQUIRE(ui->lab().command(R"({"do": "take", "supply": "flask", "at": [12, 6]})", why));
	ui->frame(1.0 / 60.0);
	const auto held_over_first_box = [&](const std::string &query) {
		type(*ui, "search:supplies", query); // (the first box shows what the search leaves)
		float fx = 0, fy = 0;
		REQUIRE(ui->locate("item:flask", fx, fy));
		ui->pointer(fx, fy, true);
		ui->pointer(fx, fy, false);
		ui->pointer(60.0f, 150.0f, false); // over Supplies' first box
		std::vector<double> seen = held_colours(*ui);
		ui->key(lab::Ui::KeyEscape, 0); // back where it stood
		float sx = 0, sy = 0;
		REQUIRE(ui->locate("search:supplies", sx, sy));
		ui->pointer(sx, sy, true);
		ui->pointer(sx, sy, false);
		ui->key(lab::Ui::KeyEscape, 0); // the search cleared
		return seen;
	};
	const std::vector<double> over_beaker = held_over_first_box("beaker"), over_camera = held_over_first_box("camera");
	REQUIRE(!over_beaker.empty());
	CHECK(over_beaker == over_camera); // whatever box is behind it, it sees the same wall
}

namespace {

// every pixel whose ray goes through a held flask's glass (its rims too: where rays are bent far), relative to its
// rectangle, the rays at a middle wavelength
std::vector<double> glass_colours(lab::Ui &ui) {
	const lab::OpticsScene &scene = ui.optics();
	const int b = lab::optics_body(scene, "flask");
	REQUIRE(b >= 0);
	const float *r = &scene.table[static_cast<size_t>(lab::optics_layout::HEADER_VECS + b * lab::optics_layout::BODY_VECS) * 4];
	std::vector<double> out;
	for (int dy = 0; dy < static_cast<int>(r[3]); ++dy) {
		for (int dx = 0; dx < static_cast<int>(r[2]); ++dx) {
			const int x = static_cast<int>(r[0]) + dx, y = static_cast<int>(r[1]) + dy;
			const lab::OpticsRay ray = lab::optics_ray(scene, x + 0.5, y + 0.5, 550.0);
			if (ray.bent > 0 && ray.body[0] == b) {
				double rgba[4];
				lab::optics_pixel(scene, x, y, rgba);
				out.insert(out.end(), { static_cast<double>(dx), static_cast<double>(dy), rgba[0], rgba[1], rgba[2] });
			}
		}
	}
	return out;
}

} // namespace

TEST_CASE("supplies: a flask held over Supplies sees through its glass exactly what it sees with nothing in Supplies there, its rims too (Stage 61)") {
	// (the user: its rims pixelated over Supplies, not over the bench: a ray seen through what is in hand met the
	// panel's boxes beside it, bent far at a rim, or passing its glass where its outline still covers the pixel)
	const auto held_at = [](const std::string &bench, float x, float y) {
		std::string why;
		auto ui = lab::Ui::open(bench, why);
		REQUIRE_MESSAGE(ui != nullptr, why);
		REQUIRE(ui->lab().command(R"({"do": "take", "supply": "flask", "at": [20, 1]})", why));
		ui->frame(1.0 / 60.0);
		float fx = 0, fy = 0;
		REQUIRE(ui->locate("item:flask", fx, fy));
		ui->pointer(fx, fy, true);
		ui->pointer(fx, fy, false);
		ui->pointer(x, y, false);
		return glass_colours(*ui);
	};
	// the free lab, its boxes under the flask; and a lab whose only supply is the flask (nothing in Supplies there)
	const char *const ALONE = R"({"format": "chem-bench/2", "room": {"celsius": 20, "air_mol": 1000}, "items": [
		{"id": "flask", "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "conical_flask", "wall_g": 100}]})";
	for (const float y : { 500.0f, 620.0f }) {
		const std::vector<double> over_boxes = held_at(read("data/benches/lab.json"), 60.0f, y), over_nothing = held_at(ALONE, 60.0f, y);
		REQUIRE(over_boxes.size() > 1000);
		REQUIRE(over_boxes.size() == over_nothing.size());
		size_t differ = 0;
		double worst = 0.0;
		std::ostringstream where;
		for (size_t k = 0; k < over_boxes.size(); k += 5) {
			const double d = std::fabs(over_boxes[k + 2] - over_nothing[k + 2]) + std::fabs(over_boxes[k + 3] - over_nothing[k + 3]) + std::fabs(over_boxes[k + 4] - over_nothing[k + 4]);
			differ += d > 0.0 ? 1 : 0;
			if (d > worst) {
				worst = d;
				where.str("");
				where << "(" << over_boxes[k] << ", " << over_boxes[k + 1] << ")";
			}
		}
		MESSAGE("at " << y << ": " << differ << " of " << over_boxes.size() / 5 << " differ, the most " << worst << " at " << where.str());
		// (a pixel at its mouth's edge reaches past the glass beside the opening, and sees what is there: under an
		// 8-bit step, and a few of thousands)
		CHECK(worst < 3.0 / 255.0);
		CHECK(differ * 200 < over_boxes.size() / 5);
	}
}
