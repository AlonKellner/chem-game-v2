// A frame's cost the same at any speed of the clock (Stage 36; GAME_SPEC §13.1): the screen runs a frame's steps
// under a budget of the CPU's time and carries the rest; the steps are the steps however they fall (D1). The
// budget's clock is the test's own: each step costs a fixed time, so what a frame runs is exact.
#include "lab/lab.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <memory>
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

void press(lab::Ui &ui, const std::string &what) {
	float x = 0.0f, y = 0.0f;
	REQUIRE_MESSAGE(ui.locate(what, x, y), what);
	ui.pointer(x, y, true);
	ui.pointer(x, y, false);
}

// the demo's bench with the flask heated, its malachite decomposing, at 600 times the clock's speed
std::unique_ptr<lab::Ui> busy() {
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/demo.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	for (const char *c : { R"({"do": "heat", "burner": "burner", "vessel": "flask"})", R"({"do": "tip", "species": "malachite", "from": "jar", "to": "flask", "all": true})",
				 R"({"do": "valve", "burner": "burner", "to": 1})" }) {
		REQUIRE_MESSAGE(ui->lab().command(c, why), why);
	}
	ui->frame(1.0 / 120.0);
	press(*ui, "speed:600");
	return ui;
}

} // namespace

TEST_CASE("speed: under a budget a frame runs the steps its time holds and carries the rest; the lab is the lab of the same steps, and no more than a frame's worth is owed (Stage 36)") {
	std::unique_ptr<lab::Ui> ui = busy();
	lab::Ui &u = *ui;
	// each step costs the budget's clock 0.05 ms: a budget of 3 ms holds 60 steps, where a frame at 600x asks 300
	u.set_clock([&u] { return 0.05 * static_cast<double>(u.lab().step()); });
	u.set_budget(3.0);
	for (int f = 0; f < 120; ++f) {
		const int64_t before = u.lab().step();
		u.frame(1.0 / 120.0);
		const int64_t ran = u.lab().step() - before;
		CHECK(ran >= 60);
		CHECK(ran <= 61);
	}
	// the clock kept to a fifth of the speed asked: 60 steps a frame, 120 frames a second, is 120 times the clock
	CHECK(u.achieved() == doctest::Approx(120.0).epsilon(0.02));
	CHECK(u.achieved() < 0.9 * u.speed());
	// the same steps, run straight through, give the same lab (D1)
	std::string why;
	std::unique_ptr<lab::Ui> straight = busy();
	const int64_t to = u.lab().step() - straight->lab().step();
	REQUIRE(straight->lab().advance(to, why));
	CHECK(straight->lab().digest() == u.lab().digest());
	// the budget let go: what was carried is at most a frame's worth, so the next frame runs at most two frames' steps
	u.set_budget(0.0);
	const int64_t before = u.lab().step();
	u.frame(1.0 / 120.0);
	CHECK(u.lab().step() - before <= 600);
	CHECK(u.lab().step() - before >= 300);
	// and then every step asked, as with no budget at all
	const int64_t next = u.lab().step();
	u.frame(1.0 / 120.0);
	CHECK(u.lab().step() - next == 300);
}

TEST_CASE("speed: a budget that holds the frame's steps changes nothing; a step that costs more than the budget is still run, one a frame (Stage 36)") {
	std::unique_ptr<lab::Ui> ui = busy();
	lab::Ui &u = *ui;
	u.set_clock([&u] { return 0.001 * static_cast<double>(u.lab().step()); });
	u.set_budget(3.0); // 3,000 steps' worth: a frame's 300 fit
	for (int f = 0; f < 10; ++f) {
		const int64_t before = u.lab().step();
		u.frame(1.0 / 120.0);
		CHECK(u.lab().step() - before == 300);
	}
	// each step costs ten budgets: the clock creeps, one step a frame, and the picture keeps its frames
	u.set_clock([&u] { return 30.0 * static_cast<double>(u.lab().step()); });
	for (int f = 0; f < 10; ++f) {
		const int64_t before = u.lab().step();
		u.frame(1.0 / 120.0);
		CHECK(u.lab().step() - before == 1);
	}
}
