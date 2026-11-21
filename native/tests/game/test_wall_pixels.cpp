// The wall the optical pass sees is the wall the host paints, pixel for pixel (GAME_SPEC §12.3): each shape of
// the draw list's first part on the pixels whose centres it covers, an outline's stroke centred on its edges.
// Where a vessel's rectangle paints the wall itself, around its glass, it then shows what is drawn beside it: no
// box around a vessel (a line one pixel wide on a whole coordinate split into two rows of half its darkness was one).
#include "lab/optics_scene.hpp"
#include "lab/ui.hpp"
#include <doctest/doctest.h>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace {

std::string read_file(const char *relative) {
	std::ifstream in(std::string(CHEM_REPO_ROOT) + "/" + relative);
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}

double linear(uint32_t c, int shift) {
	const double v = ((c >> shift) & 0xff) / 255.0;
	return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

} // namespace

TEST_CASE("wall pixels: every pixel of the wall is one colour the host paints there, never a share of two (Stage 25)") {
	std::string why;
	std::unique_ptr<lab::Ui> ui = lab::Ui::open(read_file("data/benches/lab.json"), why);
	REQUIRE_MESSAGE(ui != nullptr, why);
	REQUIRE_MESSAGE(ui->lab().command(R"({"do": "place", "item": "flask", "at": [10, 6]})", why), why);
	ui->frame(0.0);
	const lab::DrawList &list = ui->draw();
	const lab::OpticsScene &scene = ui->optics();
	// what may be painted: each shape's grey (the wall's luminance) and the bench's own
	std::set<long> painted{ std::lround(0.7305 * 1e4) };
	for (int i = 0; i < list.above; ++i) {
		const uint32_t c = list.commands[static_cast<size_t>(i)].color;
		painted.insert(std::lround((0.2126 * linear(c, 24) + 0.7152 * linear(c, 16) + 0.0722 * linear(c, 8)) * 1e4));
	}
	// the bench's grid lines, where no vessel stands (the flask is to the right), and the outbox's outlined corner
	const int windows[2][4] = { { 270, 300, 200, 200 }, { 240, 740, 60, 60 } };
	int checked = 0, mixed = 0;
	for (const auto &w : windows) {
		for (int y = w[1]; y < w[1] + w[3]; ++y) {
			for (int x = w[0]; x < w[0] + w[2]; ++x) {
				double rgb[3];
				lab::optics_truth(scene, x, y, rgb);
				++checked;
				if (painted.count(std::lround(rgb[1] * 1e4)) == 0) {
					++mixed;
					if (mixed <= 5) {
						MESSAGE("a share of two colours at (" << x << ", " << y << "): " << rgb[1]);
					}
				}
			}
		}
	}
	CHECK(checked == 200 * 200 + 60 * 60);
	CHECK(mixed == 0);
}
