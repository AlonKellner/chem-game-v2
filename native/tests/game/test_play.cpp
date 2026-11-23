// A play (Stage 33): gestures as data, a frame at a time. The play whose frames a host times (make pass-cost) is
// played here without one, and what it is meant to do is held: a play that stopped doing it would time nothing.
#include "lab/play.hpp"
#include "lab/ui.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <map>
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

const lab::ItemView &item(const lab::LabView &view, const std::string &id) {
	for (const lab::ItemView &v : view.items) {
		if (v.id == id) {
			return v;
		}
	}
	FAIL("no item " << id);
	return view.items.front();
}

} // namespace

TEST_CASE("play: a play that is not one is refused, with the reason (Stage 33)") {
	lab::Play play;
	std::string why;
	CHECK_FALSE(lab::Play::read("{}", play, why));
	CHECK_FALSE(why.empty());
	why.clear();
	CHECK_FALSE(lab::Play::read(R"({"format": "chem-play/1", "steps": [{"button": "twice"}]})", play, why));
	CHECK(why.find("twice") != std::string::npos);
	why.clear();
	CHECK_FALSE(lab::Play::read(R"({"format": "chem-play/1", "steps": [{"wait": 3}]})", play, why));
	CHECK(why.find("wait") != std::string::npos);
	CHECK(lab::Play::read(R"({"format": "chem-play/1", "steps": [{"to": [10, 20], "frames": 3, "mark": "there"}]})", play, why));
	CHECK_FALSE(play.over());
}

TEST_CASE("play: the play a host times sets the bench up, carries the bottle, pours it by hand and stirs, then runs at 600x (Stage 33, 36)") {
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read("data/benches/lab.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	lab::Play play;
	REQUIRE_MESSAGE(lab::Play::read(read("data/plays/cost.json"), play, why), why);
	std::map<std::string, int> frames;
	double most_turn = 0.0, most_pour = 0.0;
	for (int i = 0; i < 6000 && play.next(*ui); ++i) {
		ui->frame(1.0 / 120.0);
		++frames[play.mark()];
		const lab::LabView now = ui->lab().view();
		const lab::ItemView &acid = item(now, "acid");
		most_turn = std::max(most_turn, acid.turn);
		most_pour = std::max(most_pour, acid.pours_ml_s);
	}
	CHECK(play.over());
	CHECK_MESSAGE(play.missed().empty(), "not on the screen when its step came: " << play.missed());
	CHECK(frames["settled"] == 60);
	CHECK(frames["carry"] == 90);
	CHECK(frames["pour"] == 421);
	CHECK(frames["stir"] == 360);
	CHECK(frames["fast"] == 360); // (Stage 36: and on at 600 times the clock's speed)
	const lab::LabView view = ui->lab().view();
	const lab::ItemView &flask = item(view, "flask");
	const lab::ItemView &acid = item(view, "acid");
	// the bottle was turned past level and ran; most of it is in the flask, with the powder tipped in before
	CHECK(most_turn > 1.5);
	CHECK(most_pour > 20.0);
	CHECK(flask.liquid_ml > 150.0);
	CHECK(acid.liquid_ml < 100.0);
	CHECK(flask.place == "bench");
	CHECK(acid.place == "bench");
	// stirred: a vortex stands in it, and it holds powder up
	CHECK(flask.swirl_j > 0.0);
	CHECK(flask.vortex > 0.0);
	CHECK(flask.suspended > 0.0);
}
