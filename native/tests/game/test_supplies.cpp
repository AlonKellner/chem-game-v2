// Supplies and Storage (GAME_SPEC §13, Stage 61): what a bench gives without a square is a supply, a template no
// part of the room's world, of which each copy taken out joins it (the ledger counting what came in: C9); Storage, the
// outbox, holds what is put aside, still simulated, in as many slots as it needs.
#include "lab/lab.hpp"

#include <doctest/doctest.h>

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
