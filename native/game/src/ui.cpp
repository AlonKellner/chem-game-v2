#include "lab/ui.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace lab {

using json = nlohmann::json;

namespace {

struct Rect {
	float x = 0, y = 0, w = 0, h = 0;
	bool has(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
	float cx() const { return x + w / 2; }
	float cy() const { return y + h / 2; }
};

// the screen's fixed parts
constexpr Rect CABINET{ 0, 64, 240, 1016 };
constexpr Rect BENCH{ 260, 120, 1120, 600 };
constexpr Rect OUTBOX{ 260, 760, 1120, 300 };
constexpr Rect READOUT{ 1400, 64, 520, 1016 };
constexpr Rect PANEL{ 600, 250, 440, 300 };
constexpr float SPEEDS[] = { 1, 10, 60, 600 };

constexpr uint32_t BACKGROUND = 0x2a2a2aff, GRID = 0x444444ff, GLASS = 0xffffffcc, INK = 0xe6e6e6ff, DIM = 0x9a9a9aff, PANEL_FILL = 0x383838ff, BUTTON = 0x505050ff,
				   BUTTON_ON = 0x3c7a4eff, WARN = 0xe0a040ff;

uint32_t rgba(int r, int g, int b, int a = 255) {
	return static_cast<uint32_t>(r) << 24 | static_cast<uint32_t>(g) << 16 | static_cast<uint32_t>(b) << 8 | static_cast<uint32_t>(a);
}

uint32_t mix(uint32_t a, uint32_t b, double t) {
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	uint32_t out = 0;
	for (int shift = 24; shift >= 0; shift -= 8) {
		const double ca = (a >> shift) & 0xff, cb = (b >> shift) & 0xff;
		out |= static_cast<uint32_t>(ca + (cb - ca) * t + 0.5) << shift;
	}
	return out;
}

// the flat colour of a solid (Stage 10 replaces these with its optics)
uint32_t solid_colour(const std::string &species) {
	if (species == "malachite") {
		return rgba(70, 150, 110);
	}
	if (species == "copper_oxide") {
		return rgba(30, 28, 28);
	}
	if (species == "copper_sulfate_pentahydrate") {
		return rgba(35, 95, 200);
	}
	if (species == "copper_sulfate") {
		return rgba(235, 235, 225);
	}
	return rgba(170, 170, 170);
}

std::string text_of(const char *format, double a = 0, double b = 0) {
	char buffer[160];
	std::snprintf(buffer, sizeof buffer, format, a, b);
	return buffer;
}

} // namespace

struct Ui::Impl {
	std::unique_ptr<Lab> lab;
	std::string bench; // to open it again
	LabView view;
	DrawList list;
	std::string message;
	double speed = 1.0;
	bool paused = false;
	bool check_every_step = false;
	double owed = 0.0; // steps not yet taken
	// the pointer
	float px = 0, py = 0;
	bool down = false;
	int drag = -1; // the item in hand
	float grab_x = 0, grab_y = 0; // where in it it was taken
	bool moved = false;
	int valve_drag = -1; // the burner whose valve is being turned
	bool slider_drag = false;
	int hover = -1;
	// the pour panel
	bool panel = false;
	int from = -1, to = -1;
	double pour_fraction = 1.0;

	void refresh() { view = lab->view(); }

	float cell_w() const { return std::fmin(140.0f, BENCH.w / static_cast<float>(view.cols)); }
	float cell_h() const { return std::fmin(300.0f, BENCH.h / static_cast<float>(view.rows)); }
	Rect cell(int col, int row) const { return Rect{ BENCH.x + static_cast<float>(col) * cell_w(), BENCH.y + static_cast<float>(row) * cell_h(), cell_w(), cell_h() }; }

	// how large an item is drawn
	void size_of(const ItemView &v, float &w, float &h) const {
		if (v.kind == "burner") {
			w = 56;
			h = 26;
		} else if (v.kind == "balloon") {
			const double r = v.burst ? 10.0 : std::fmin(90.0, 14.0 + 6.0 * std::cbrt(v.gas_ml + v.condensed_ml));
			w = h = static_cast<float>(2.0 * r);
		} else if (v.condenser) {
			w = 44;
			h = 120;
		} else {
			w = static_cast<float>(40.0 + 8.0 * std::cbrt(v.volume_ml));
			h = w * 1.25f;
		}
	}

	// its slot among the items of a place, in order
	int slot(int index, const char *place) const {
		int n = 0;
		for (int i = 0; i < index; ++i) {
			if (view.items[static_cast<size_t>(i)].place == place && view.items[static_cast<size_t>(i)].kind != "tray") {
				++n;
			}
		}
		return n;
	}

	// where an item is drawn (not counting a drag)
	Rect rect(int index) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		float w = 0, h = 0;
		size_of(v, w, h);
		if (v.place == "cabinet") {
			const Rect box{ 20, 96 + static_cast<float>(slot(index, "cabinet")) * 118, 200, 110 };
			const float scale = std::fmin(1.0f, std::fmin(box.w / w, (box.h - 24) / h));
			return Rect{ box.cx() - w * scale / 2, box.y + box.h - 22 - h * scale, w * scale, h * scale };
		}
		if (v.place == "outbox") {
			const Rect box{ OUTBOX.x + 12 + static_cast<float>(slot(index, "outbox")) * 160, OUTBOX.y + 40, 150, 230 };
			return Rect{ box.cx() - w / 2, box.y + box.h - 30 - h, w, h };
		}
		if (v.place == "on" && v.on >= 0) {
			const Rect holder = rect(v.on);
			return Rect{ holder.cx() - w / 2, holder.y - h - 4, w, h };
		}
		if (v.place == "under" && v.under >= 0) {
			const Rect vessel = rect(v.under);
			return Rect{ vessel.cx() - w / 2, vessel.y + vessel.h + 64, w, h };
		}
		// on the bench: standing on the cell's floor, raised when a burner is under it
		const Rect c = cell(v.col, v.row);
		float lift = 34;
		for (const ItemView &other : view.items) {
			if (other.kind == "burner" && other.under == index) {
				lift = 34 + 94; // the burner, and its flame, under it
			}
		}
		return Rect{ c.cx() - w / 2, c.y + c.h - lift - h, w, h };
	}

	// the item under a point, the topmost; not the tray, not `except`
	int item_at(float x, float y, int except = -1) const {
		for (int i = static_cast<int>(view.items.size()) - 1; i >= 0; --i) {
			if (i == except || view.items[static_cast<size_t>(i)].kind == "tray") {
				continue;
			}
			Rect r = rect(i);
			r.x -= 6;
			r.y -= 6;
			r.w += 12;
			r.h += 12;
			if (r.has(x, y)) {
				return i;
			}
		}
		return -1;
	}

	// a burner's valve: a slider beside it, while it is on the bench or under a vessel
	bool valve_rect(int index, Rect &r) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		if (v.kind != "burner" || (v.place != "bench" && v.place != "under")) {
			return false;
		}
		const Rect body = rect(index);
		r = Rect{ body.x + body.w + 10, body.y + body.h - 84, 16, 84 };
		return true;
	}

	// a vessel's stirring button, while it stands on the bench with liquid in it
	bool stir_rect(int index, Rect &r) const {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		if (v.kind != "vessel" || v.place != "bench" || !(v.liquid_ml > 0.0)) {
			return false;
		}
		const Rect c = cell(v.col, v.row);
		r = Rect{ c.x + 4, c.y + 6, 44, 24 }; // the cell's corner, clear of what stands on the vessel's mouth
		return true;
	}

	Rect speed_rect(int i) const { return Rect{ 380 + static_cast<float>(i) * 84, 14, 76, 36 }; }
	static Rect pause_rect() { return Rect{ 270, 14, 96, 36 }; }
	static Rect check_rect() { return Rect{ 760, 14, 220, 36 }; }
	static Rect reset_rect() { return Rect{ 1000, 14, 96, 36 }; }
	static Rect pour_slider() { return Rect{ PANEL.x + 30, PANEL.y + 84, PANEL.w - 60, 18 }; }
	static Rect pour_button() { return Rect{ PANEL.x + 30, PANEL.y + 118, 180, 36 }; }
	static Rect cancel_button() { return Rect{ PANEL.x + PANEL.w - 126, PANEL.y + PANEL.h - 50, 96, 36 }; }
	// the solids of the vessel being emptied, each with its button
	std::vector<std::pair<std::string, Rect>> tip_buttons() const {
		std::vector<std::pair<std::string, Rect>> out;
		if (!panel || from < 0) {
			return out;
		}
		for (const SubstanceView &s : view.items[static_cast<size_t>(from)].contents) {
			if (s.phase == 's' && !s.wall && out.size() < 3) {
				out.emplace_back(s.species, Rect{ PANEL.x + 30, PANEL.y + 168 + static_cast<float>(out.size()) * 40, 260, 34 });
			}
		}
		return out;
	}

	bool act(const json &command) {
		std::string why;
		const bool ok = lab->command(command.dump(), why);
		message = ok ? std::string() : why;
		refresh();
		return ok;
	}

	const std::string &id(int index) const { return view.items[static_cast<size_t>(index)].id; }

	// what letting go of an item at a point does
	void drop(int index, float x, float y) {
		const ItemView v = view.items[static_cast<size_t>(index)];
		const int target = item_at(x, y, index);
		// first it comes off what it is joined to
		const bool was_on = v.place == "on", was_under = v.place == "under";
		if (target >= 0 && (target == v.on || target == v.under)) {
			return; // put back where it was
		}
		if (was_on && !act({ { "do", "detach" }, { "item", v.id } })) {
			return;
		}
		if (was_under && !act({ { "do", "unheat" }, { "burner", v.id } })) {
			return;
		}
		if (target >= 0) {
			const ItemView t = view.items[static_cast<size_t>(target)];
			if (v.kind == "burner") {
				if (t.kind == "vessel") {
					act({ { "do", "heat" }, { "burner", v.id }, { "vessel", t.id } });
				}
			} else if (t.kind != "vessel") {
				message = "\"" + t.id + "\" takes nothing";
			} else if (v.kind == "balloon" || v.condenser) {
				act({ { "do", "attach" }, { "item", v.id }, { "to", t.id } });
			} else if (t.condenser) {
				act({ { "do", "under" }, { "item", v.id }, { "of", t.id } });
			} else {
				// a vessel over a vessel: ask what to pour or tip
				panel = true;
				from = index;
				to = target;
				pour_fraction = v.liquid_ml > 50.0 ? 50.0 / v.liquid_ml : 1.0;
			}
			return;
		}
		if (OUTBOX.has(x, y)) {
			act({ { "do", "submit" }, { "item", v.id } });
			return;
		}
		if (CABINET.has(x, y)) {
			if (view.items[static_cast<size_t>(index)].place == "bench") {
				act({ { "do", "stow" }, { "item", v.id } });
			}
			return;
		}
		if (BENCH.has(x, y)) {
			const int col = static_cast<int>((x - BENCH.x) / cell_w()), row = static_cast<int>((y - BENCH.y) / cell_h());
			if (col < view.cols && row < view.rows) {
				act({ { "do", "place" }, { "item", v.id }, { "at", { col, row } } });
			}
		}
	}

	void press(float x, float y) {
		if (panel) {
			if (pour_slider().has(x, y) && view.items[static_cast<size_t>(from)].liquid_ml > 0.0) {
				slider_drag = true;
				slide(x);
				return;
			}
			if (pour_button().has(x, y) && view.items[static_cast<size_t>(from)].liquid_ml > 0.0) {
				const double ml = view.items[static_cast<size_t>(from)].liquid_ml * pour_fraction;
				json command = { { "do", "pour" }, { "from", id(from) }, { "to", id(to) } };
				if (pour_fraction >= 1.0) {
					command["all"] = true;
				} else {
					command["ml"] = ml;
				}
				if (act(command)) {
					panel = false;
				}
				return;
			}
			for (const auto &[species, r] : tip_buttons()) {
				if (r.has(x, y)) {
					if (act({ { "do", "tip" }, { "from", id(from) }, { "to", id(to) }, { "species", species }, { "all", true } })) {
						panel = false;
					}
					return;
				}
			}
			if (cancel_button().has(x, y) || !PANEL.has(x, y)) {
				panel = false;
			}
			return;
		}
		if (pause_rect().has(x, y)) {
			paused = !paused;
			return;
		}
		for (int i = 0; i < 4; ++i) {
			if (speed_rect(i).has(x, y)) {
				speed = SPEEDS[i];
				paused = false;
				return;
			}
		}
		if (check_rect().has(x, y)) {
			check_every_step = !check_every_step;
			return;
		}
		if (reset_rect().has(x, y)) {
			std::string why;
			std::unique_ptr<Lab> again = Lab::open(bench, why);
			if (again) {
				lab = std::move(again);
				owed = 0.0;
				message.clear();
				refresh();
			}
			return;
		}
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			Rect r;
			if (valve_rect(i, r)) {
				Rect wide = r;
				wide.x -= 8;
				wide.w += 16;
				wide.y -= 6;
				wide.h += 12;
				if (wide.has(x, y)) {
					valve_drag = i;
					turn(y);
					return;
				}
			}
			if (stir_rect(i, r) && r.has(x, y)) {
				act({ { "do", "stir" }, { "vessel", id(i) }, { "on", !view.items[static_cast<size_t>(i)].stirred } });
				return;
			}
		}
		const int hit = item_at(x, y);
		if (hit >= 0 && view.items[static_cast<size_t>(hit)].place != "outbox") {
			const Rect r = rect(hit);
			drag = hit;
			grab_x = x - r.x;
			grab_y = y - r.y;
			moved = false;
		}
	}

	void slide(float x) {
		const Rect r = pour_slider();
		const double f = (x - r.x) / r.w;
		pour_fraction = f < 0.02 ? 0.02 : f > 1 ? 1 : f;
	}

	void turn(float y) {
		Rect r;
		if (!valve_rect(valve_drag, r)) {
			return;
		}
		double open = 1.0 - (y - r.y) / r.h;
		open = open < 0 ? 0 : open > 1 ? 1 : open;
		act({ { "do", "valve" }, { "burner", id(valve_drag) }, { "to", open } });
	}

	// ------------------------------------------------------------- painting

	void rect_fill(const Rect &r, uint32_t color) {
		list.commands.push_back(DrawCommand{ DrawCommand::Rect, color, 1, 0, 0, r.x, r.y, r.w, r.h });
	}
	void outline(const Rect &r, uint32_t color, float width = 2) {
		list.commands.push_back(DrawCommand{ DrawCommand::Outline, color, width, 0, 0, r.x, r.y, r.w, r.h });
	}
	void line(float x1, float y1, float x2, float y2, uint32_t color, float width = 2) {
		list.commands.push_back(DrawCommand{ DrawCommand::Line, color, width, 0, 0, x1, y1, x2, y2 });
	}
	void circle(float x, float y, float r, uint32_t color) {
		list.commands.push_back(DrawCommand{ DrawCommand::Circle, color, 1, 0, 0, x, y, r, 0 });
	}
	void polygon(std::initializer_list<float> xy, uint32_t color) {
		const auto first = static_cast<int32_t>(list.points.size() / 2);
		list.points.insert(list.points.end(), xy.begin(), xy.end());
		list.commands.push_back(DrawCommand{ DrawCommand::Polygon, color, 1, first, static_cast<int32_t>(xy.size() / 2), 0, 0, 0, 0 });
	}
	void text(float x, float y, const std::string &s, uint32_t color = INK, float size = 18) {
		const auto offset = static_cast<int32_t>(list.text.size());
		list.text += s;
		list.text += '\0';
		list.commands.push_back(DrawCommand{ DrawCommand::Text, color, 1, offset, static_cast<int32_t>(s.size()), x, y, 0, size });
	}
	void button(const Rect &r, const std::string &label, bool on = false) {
		rect_fill(r, on ? BUTTON_ON : BUTTON);
		text(r.x + 10, r.y + r.h / 2 + 6, label);
	}

	void paint_item(int index, Rect r) {
		const ItemView &v = view.items[static_cast<size_t>(index)];
		if (v.kind == "burner") {
			rect_fill(r, rgba(96, 96, 96));
			if (v.valve > 0.0) {
				const float flame = static_cast<float>(12.0 + 30.0 * v.valve);
				polygon({ r.cx() - 12, r.y, r.cx() + 12, r.y, r.cx(), r.y - flame }, mix(rgba(240, 200, 60), rgba(80, 140, 255), v.valve));
			}
		} else if (v.kind == "balloon") {
			circle(r.cx(), r.cy(), r.w / 2, v.burst ? rgba(120, 60, 60) : rgba(220, 90, 90, 200));
			if (v.condensed_ml > 0.01 && !v.burst) {
				circle(r.cx(), r.y + r.h - 6, 4, rgba(150, 190, 235)); // the water that condensed in it
			}
		} else {
			// what is in it: liquid up to its level, solids as a mound on the floor
			double solid_ml = 0.0, salt = 0.0, water = 0.0, most = 0.0;
			std::string solid;
			for (const SubstanceView &s : v.contents) {
				if (s.wall) {
					continue;
				}
				if (s.phase == 's') {
					solid_ml += s.g; // roughly a mL a gram: enough for a mound
					if (s.g > most) {
						most = s.g;
						solid = s.species;
					}
				}
				if (s.species == "copper_sulfate_aq") {
					salt = s.mol;
				}
				if (s.species == "water") {
					water = s.g;
				}
			}
			const float level = static_cast<float>(std::fmin(1.0, v.condensed_ml / (v.volume_ml > 0.0 ? v.volume_ml : 1.0))) * r.h;
			if (v.liquid_ml > 0.05) {
				const double molal = water > 0.0 ? salt / (water / 1000.0) : 0.0;
				rect_fill(Rect{ r.x + 2, r.y + r.h - level, r.w - 4, level }, mix(rgba(120, 170, 220, 110), rgba(30, 90, 220, 220), molal / 1.5));
			}
			if (solid_ml > 0.01) { // a few milligrams are not a mound
				const float mound = static_cast<float>(std::fmin(static_cast<double>(r.h) * 0.5, 9.0 * std::cbrt(solid_ml)));
				polygon({ r.x + 4, r.y + r.h - 2, r.x + r.w - 4, r.y + r.h - 2, r.x + r.w * 0.7f, r.y + r.h - 2 - mound, r.x + r.w * 0.3f, r.y + r.h - 2 - mound }, solid_colour(solid));
				line(r.x + r.w * 0.3f, r.y + r.h - 2 - mound, r.x + r.w * 0.7f, r.y + r.h - 2 - mound, rgba(150, 150, 150, 160), 1); // a dark solid on a dark bench
			}
			// the glass: open at the top
			line(r.x, r.y, r.x, r.y + r.h, GLASS);
			line(r.x, r.y + r.h, r.x + r.w, r.y + r.h, GLASS);
			line(r.x + r.w, r.y + r.h, r.x + r.w, r.y, GLASS);
			if (v.condenser) {
				line(r.x + r.w, r.y + r.h - 10, r.x + r.w + 26, r.y + r.h + 14, GLASS); // its outlet
			}
		}
		const std::string label = v.kind == "burner" ? v.id : v.id + text_of("  %.0f C", v.celsius);
		if (index == drag) {
			return;
		}
		if (v.place == "on") {
			text(r.x, r.y - 8, label, DIM, 15);
		} else if (v.place == "under") {
			// under a vessel it needs no name: its valve is beside it
		} else if (v.place == "bench") {
			const Rect c = cell(v.col, v.row);
			text(c.x + 8, c.y + c.h - 10, label, DIM, 15);
		} else {
			text(r.x, r.y + r.h + 18, label, DIM, 15);
		}
	}

	void paint() {
		list.commands.clear();
		list.points.clear();
		list.text.clear();
		rect_fill(Rect{ 0, 0, 1920, 1080 }, BACKGROUND);
		// the bench and its grid
		for (int c = 0; c <= view.cols; ++c) {
			line(BENCH.x + static_cast<float>(c) * cell_w(), BENCH.y, BENCH.x + static_cast<float>(c) * cell_w(), BENCH.y + static_cast<float>(view.rows) * cell_h(), GRID, 1);
		}
		for (int r = 0; r <= view.rows; ++r) {
			line(BENCH.x, BENCH.y + static_cast<float>(r) * cell_h(), BENCH.x + static_cast<float>(view.cols) * cell_w(), BENCH.y + static_cast<float>(r) * cell_h(), GRID, 1);
		}
		outline(CABINET, GRID, 1);
		text(CABINET.x + 16, CABINET.y + 24, "cabinet", DIM);
		outline(OUTBOX, GRID, 1);
		text(OUTBOX.x + 16, OUTBOX.y + 26, "outbox: drop here to hand in", DIM);
		// the clock
		button(pause_rect(), paused ? "play" : "pause", paused);
		for (int i = 0; i < 4; ++i) {
			button(speed_rect(i), text_of("%.0fx", SPEEDS[i]), !paused && speed == SPEEDS[i]);
		}
		button(check_rect(), check_every_step ? "check every step: on" : "check every step: off", check_every_step);
		button(reset_rect(), "reset");
		const double seconds = static_cast<double>(view.step) / 60.0;
		text(1120, 40, text_of("%02.0f:%04.1f", std::floor(seconds / 60.0), seconds - 60.0 * std::floor(seconds / 60.0)), INK, 22);
		// the items, those in hand last
		for (int i = 0; i < static_cast<int>(view.items.size()); ++i) {
			if (view.items[static_cast<size_t>(i)].kind == "tray" || i == drag) {
				continue;
			}
			paint_item(i, rect(i));
			Rect r;
			if (valve_rect(i, r)) {
				rect_fill(r, BUTTON);
				const float knob = r.y + r.h * static_cast<float>(1.0 - view.items[static_cast<size_t>(i)].valve);
				rect_fill(Rect{ r.x - 4, knob - 4, r.w + 8, 8 }, INK);
			}
			if (stir_rect(i, r)) {
				button(r, "stir", view.items[static_cast<size_t>(i)].stirred);
			}
			// what stands under a condenser's outlet is joined to it by a drip
			const ItemView &v = view.items[static_cast<size_t>(i)];
			if (v.drains_to >= 0) {
				const Rect a = rect(i), b = rect(v.drains_to);
				line(a.x + a.w + 26, a.y + a.h + 14, b.cx(), b.y, rgba(150, 190, 235, 160), 1);
			}
		}
		if (drag >= 0) {
			Rect r = rect(drag);
			r.x = px - grab_x;
			r.y = py - grab_y;
			paint_item(drag, r);
		}
		paint_readout();
		if (panel) {
			paint_panel();
		}
	}

	void paint_readout() {
		float y = READOUT.y + 30;
		const auto row = [&](const std::string &s, uint32_t color = INK, float size = 17) {
			text(READOUT.x + 16, y, s, color, size);
			y += size + 8;
		};
		outline(READOUT, GRID, 1);
		row(view.conserved ? "every element, mass and energy: unchanged" : "THE TOTALS MOVED", view.conserved ? rgba(120, 200, 140) : rgba(240, 90, 90));
		row(text_of("world: %.3f g, %.3f kJ", view.mass_g, view.energy_j / 1000.0), DIM, 15);
		y += 10;
		const int shown = drag >= 0 ? drag : hover;
		if (shown >= 0) {
			const ItemView &v = view.items[static_cast<size_t>(shown)];
			row(v.id + " (" + v.kind + (v.condenser ? ", condenser" : "") + ", " + v.place + ")", INK, 20);
			if (v.kind == "burner") {
				row(text_of("flame %.0f C, valve %.0f%%", v.celsius, 100.0 * v.valve));
			} else {
				row(text_of("%.2f C", v.celsius));
				row(v.kind == "vessel" ? text_of("%.1f mL gas, %.2f mL solids and liquids", v.gas_ml, v.condensed_ml)
									   : text_of("%.1f mL of %.0f mL", v.gas_ml + v.condensed_ml, v.capacity_ml));
				for (const SubstanceView &s : v.contents) {
					if (s.wall) {
						continue;
					}
					const std::string phase = s.phase == 'a' ? "aq" : std::string(1, s.phase);
					std::string line = s.formula + "(" + phase + ")" + text_of("  %.4f g", s.g);
					if (s.particles > 0) {
						line += text_of("  %.0f particles, %.0f um", static_cast<double>(s.particles), s.size_um);
					}
					row(line, DIM, 15);
				}
			}
		} else {
			row("point at something to read it", DIM);
		}
		y += 10;
		for (const std::string &e : view.events) {
			row(e, WARN, 15);
		}
		if (!message.empty()) {
			// wrapped at about 52 characters
			for (size_t at = 0; at < message.size(); at += 52) {
				text(READOUT.x + 16, READOUT.y + READOUT.h - 120 + static_cast<float>(at / 52) * 24, message.substr(at, 52), WARN, 16);
			}
		}
	}

	void paint_panel() {
		const ItemView &source = view.items[static_cast<size_t>(from)];
		rect_fill(PANEL, PANEL_FILL);
		outline(PANEL, GLASS, 1);
		text(PANEL.x + 30, PANEL.y + 40, source.id + " into " + id(to), INK, 22);
		if (source.liquid_ml > 0.0) {
			const Rect s = pour_slider();
			rect_fill(s, BUTTON);
			rect_fill(Rect{ s.x, s.y, s.w * static_cast<float>(pour_fraction), s.h }, rgba(90, 140, 220));
			text(s.x, s.y - 8, text_of("%.1f mL of %.1f mL", source.liquid_ml * pour_fraction, source.liquid_ml), DIM, 15);
			button(pour_button(), pour_fraction >= 1.0 ? "pour it all" : "pour");
		} else {
			text(PANEL.x + 30, PANEL.y + 100, "no liquid in it", DIM, 15);
		}
		for (const auto &[species, r] : tip_buttons()) {
			button(r, "tip in the " + species);
		}
		button(cancel_button(), "cancel");
	}
};

Ui::Ui(std::unique_ptr<Impl> impl) :
		impl_(std::move(impl)) {}
Ui::~Ui() = default;

std::unique_ptr<Ui> Ui::open(std::string_view bench_json, std::string &why) {
	auto impl = std::make_unique<Impl>();
	impl->lab = Lab::open(bench_json, why);
	if (!impl->lab) {
		return nullptr;
	}
	impl->bench = std::string(bench_json);
	impl->refresh();
	return std::unique_ptr<Ui>(new Ui(std::move(impl)));
}

void Ui::pointer(float x, float y, bool down) {
	Impl &m = *impl_;
	const bool pressed = down && !m.down, released = !down && m.down;
	if (m.drag >= 0 && (std::fabs(x - m.px) > 0 || std::fabs(y - m.py) > 0)) {
		m.moved = true;
	}
	m.px = x;
	m.py = y;
	m.down = down;
	if (pressed) {
		m.press(x, y);
	} else if (down) {
		if (m.slider_drag) {
			m.slide(x);
		} else if (m.valve_drag >= 0) {
			m.turn(y);
		}
	} else if (released) {
		if (m.drag >= 0 && m.moved) {
			const int index = m.drag;
			m.drag = -1;
			m.drop(index, x, y);
		}
		m.drag = -1;
		m.valve_drag = -1;
		m.slider_drag = false;
	}
	if (!down) {
		const int at = m.item_at(x, y);
		if (at >= 0) {
			m.hover = at;
		}
	}
}

void Ui::frame(double seconds) {
	Impl &m = *impl_;
	if (!m.paused && !m.lab->failed() && seconds > 0.0) {
		m.owed += seconds * m.speed * 60.0;
		// no more than twenty simulated seconds a frame: beyond it the clock runs slow, never the picture
		auto steps = static_cast<int64_t>(m.owed < 1200.0 ? m.owed : 1200.0);
		m.owed = m.owed < 1200.0 ? m.owed - static_cast<double>(steps) : 0.0;
		std::string why;
		bool ok = true;
		if (steps > 0) {
			// one step of every frame against every invariant; all of them when asked
			m.lab->set_checking(true);
			ok = m.lab->advance(m.check_every_step ? steps : 1, why);
			m.lab->set_checking(false);
			if (ok && !m.check_every_step && steps > 1) {
				ok = m.lab->advance(steps - 1, why);
			}
		}
		if (!ok) {
			m.message = why;
		}
	}
	m.refresh();
}

const DrawList &Ui::draw() {
	impl_->paint();
	return impl_->list;
}

Lab &Ui::lab() {
	return *impl_->lab;
}

const std::string &Ui::message() const {
	return impl_->message;
}

double Ui::speed() const {
	return impl_->speed;
}

bool Ui::paused() const {
	return impl_->paused;
}

bool Ui::locate(std::string_view what, float &x, float &y) const {
	const Impl &m = *impl_;
	const auto centre = [&](const Rect &r) {
		x = r.cx();
		y = r.cy();
		return true;
	};
	const auto index_of = [&](std::string_view name) {
		for (int i = 0; i < static_cast<int>(m.view.items.size()); ++i) {
			if (m.view.items[static_cast<size_t>(i)].id == name) {
				return i;
			}
		}
		return -1;
	};
	const auto after = [&](std::string_view prefix, std::string_view &rest) {
		if (what.substr(0, prefix.size()) != prefix) {
			return false;
		}
		rest = what.substr(prefix.size());
		return true;
	};
	std::string_view rest;
	if (what == "cabinet") {
		x = CABINET.cx();
		y = CABINET.y + CABINET.h - 60;
		return true;
	}
	if (what == "outbox") {
		x = OUTBOX.x + OUTBOX.w - 80;
		y = OUTBOX.cy();
		return true;
	}
	if (what == "pause") {
		return centre(Impl::pause_rect());
	}
	if (what == "check") {
		return centre(Impl::check_rect());
	}
	if (what == "reset") {
		return centre(Impl::reset_rect());
	}
	if (after("speed:", rest)) {
		for (int i = 0; i < 4; ++i) {
			if (rest == text_of("%.0f", SPEEDS[i])) {
				return centre(m.speed_rect(i));
			}
		}
		return false;
	}
	if (after("item:", rest)) {
		const int i = index_of(rest);
		return i >= 0 && m.view.items[static_cast<size_t>(i)].kind != "tray" && centre(m.rect(i));
	}
	if (after("cell:", rest)) {
		const size_t comma = rest.find(',');
		if (comma == std::string_view::npos) {
			return false;
		}
		const int col = std::atoi(std::string(rest.substr(0, comma)).c_str()), row = std::atoi(std::string(rest.substr(comma + 1)).c_str());
		if (col < 0 || col >= m.view.cols || row < 0 || row >= m.view.rows) {
			return false;
		}
		const Rect c = m.cell(col, row);
		x = c.cx();
		y = c.y + 60; // above whatever stands in it
		return true;
	}
	if (after("valve:", rest)) {
		const size_t at = rest.find('@');
		const int i = index_of(rest.substr(0, at));
		Rect r;
		if (i < 0 || at == std::string_view::npos || !m.valve_rect(i, r)) {
			return false;
		}
		const double to = std::atof(std::string(rest.substr(at + 1)).c_str());
		x = r.cx();
		y = r.y + r.h * static_cast<float>(1.0 - to);
		return true;
	}
	if (after("stir:", rest)) {
		const int i = index_of(rest);
		Rect r;
		return i >= 0 && m.stir_rect(i, r) && centre(r);
	}
	if (!m.panel) {
		return false;
	}
	if (what == "panel:pour") {
		return centre(Impl::pour_button());
	}
	if (what == "panel:cancel") {
		return centre(Impl::cancel_button());
	}
	if (after("panel:slider@", rest)) {
		const Rect r = Impl::pour_slider();
		x = r.x + r.w * static_cast<float>(std::atof(std::string(rest).c_str()));
		y = r.cy();
		return true;
	}
	if (after("panel:tip:", rest)) {
		for (const auto &[species, r] : m.tip_buttons()) {
			if (species == rest) {
				return centre(r);
			}
		}
	}
	return false;
}

} // namespace lab
