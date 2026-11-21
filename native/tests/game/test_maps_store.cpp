// The maps' store and the maps' layout (GAME_SPEC §12.3, Stage 24: made before the game runs; Stage 25: they lie
// where they were first put). A vessel made as glassware has its tables made ahead for every size the screen
// draws it at, on either layer, and finds them wherever it stands: nothing is traced while the game runs. And a
// map once sent to the GPU is not sent again while it is kept: what changes is only what was added.
#include "lab/optics_scene.hpp"
#include "lab/ui.hpp"
#include <doctest/doctest.h>
#include <filesystem>
#include <string>
#include <vector>

namespace {

// one vessel made as glassware, in the cabinet (its width, 40 + 8 * cbrt(50) px, is not a whole number: its axis
// on a whole pixel is then a sum the float rectangle rounds)
const char *BENCH = R"({"format": "chem-bench/2", "grid": [28, 15], "room": {"celsius": 20, "air_mol": 1000},
	"items": [{"id": "jar", "kind": "vessel", "volume_ml": 50, "wall": "glass", "glassware": "beaker", "wall_g": 40, "loss": 0.3}]})";

size_t files_in(const std::filesystem::path &dir) {
	size_t n = 0;
	for (const auto &entry : std::filesystem::directory_iterator(dir)) {
		n += entry.is_regular_file() ? 1 : 0;
	}
	return n;
}

} // namespace

TEST_CASE("maps store: a vessel finds the tables made ahead wherever it is drawn, standing or held, so none is made in play (Stage 25)") {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "chem_maps_store_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	lab::optics_store(dir.string());
	std::string why;
	{
		std::unique_ptr<lab::Ui> ahead = lab::Ui::open(BENCH, why);
		REQUIRE_MESSAGE(ahead != nullptr, why);
		CHECK(ahead->prepare_optics() >= 4); // the bench's size and the cabinet's, each on two layers
	}
	const size_t made_ahead = files_in(dir);
	REQUIRE(made_ahead >= 4);

	std::unique_ptr<lab::Ui> ui = lab::Ui::open(BENCH, why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	ui->frame(0.0);
	(void)ui->optics(); // on the cabinet's shelf
	for (const int col : { 3, 8, 13, 21 }) {
		REQUIRE_MESSAGE(ui->lab().command(R"({"do": "place", "item": "jar", "at": [)" + std::to_string(col) + R"(, 4]})", why), why);
		ui->frame(0.0);
		(void)ui->optics();
		float x = 0, y = 0;
		REQUIRE(ui->locate("item:jar", x, y));
		ui->pointer(x, y, true); // lifted, and carried a little
		ui->pointer(x + 17.3f, y - 5.6f, true);
		(void)ui->optics();
		ui->pointer(x, y, false); // set down where it stood
		(void)ui->optics();
	}
	REQUIRE_MESSAGE(ui->lab().command(R"({"do": "submit", "item": "jar", "slot": 4})", why), why);
	ui->frame(0.0);
	(void)ui->optics(); // in an outbox slot
	CHECK_MESSAGE(files_in(dir) == made_ahead, "a table was made in play: its inputs differ from those made ahead");
	lab::optics_store("");
	std::filesystem::remove_all(dir);
}

TEST_CASE("maps layout: maps lie where they were first put; one lifted and set down again changes none, and what changes is only what was added (Stage 25)") {
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(BENCH, why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	REQUIRE_MESSAGE(ui->lab().command(R"({"do": "place", "item": "jar", "at": [8, 4]})", why), why);
	ui->frame(0.0);
	std::vector<float> before = ui->optics().maps;
	uint64_t revision = ui->optics().maps_revision;
	float x = 0, y = 0;
	REQUIRE(ui->locate("item:jar", x, y));
	const auto lift_and_set_down = [&]() {
		ui->pointer(x, y, true);
		ui->pointer(x + 23.0f, y, true);
		const lab::OpticsScene &held = ui->optics();
		ui->pointer(x, y, false);
		return held.maps_revision;
	};
	// the first time it is held its maps for the nearer layer are added after what was there
	lift_and_set_down();
	const lab::OpticsScene &first = ui->optics();
	REQUIRE(first.maps_revision == revision + 1);
	REQUIRE(first.maps.size() > before.size());
	CHECK(first.maps_changed_from == static_cast<int64_t>(before.size() / 4));
	CHECK(std::equal(before.begin(), before.end(), first.maps.begin()));
	// again: both its tables are where they were
	revision = first.maps_revision;
	before = first.maps;
	CHECK(lift_and_set_down() == revision);
	CHECK(ui->optics().maps_revision == revision);
	CHECK(ui->optics().maps == before);
}
