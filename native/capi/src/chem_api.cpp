#include "chem_api.h"

#include "chem/error.hpp"
#include "chem/species.hpp"
#include "lab/lab.hpp"
#include "lab/prism_demo.hpp"
#include "lab/optics_scene.hpp"
#include "lab/play.hpp"
#include "lab/ui.hpp"

#include <algorithm>

#include <cmath>
#include <cstddef>
#include <functional>
#include <optional>
#include <memory>
#include <string>
#include <vector>

struct cg_ui {
	std::unique_ptr<lab::Ui> ui;
	lab::Play play; // the gestures given before each frame, while it runs (Stage 33)
	bool playing = false;
	std::string text; // the last report handed out
	std::vector<cg_part> parts; // the last parts handed out, and their strings
	std::vector<std::string> part_text;
};

// the draw list crosses the boundary as it is
static_assert(sizeof(cg_draw_command) == sizeof(lab::DrawCommand));
static_assert(CG_OPTICS_KEPT_VECS == lab::optics_layout::KEPT_VECS);
static_assert(CG_OPTICS_RELAY_VECS == lab::optics_layout::RELAY_RECORD);
static_assert(CG_OPTICS_RAY_POINTS == lab::optics_layout::RAY_POINTS);
static_assert(offsetof(cg_draw_command, x) == offsetof(lab::DrawCommand, x));
static_assert(offsetof(cg_draw_command, first) == offsetof(lab::DrawCommand, first));

struct cg_demo {
	std::unique_ptr<lab::PrismDemo> prism;
};

struct cg_lab {
	std::unique_ptr<lab::Lab> lab;
	std::string text; // the last report or digest handed out
};

namespace {

std::string &last_error() {
	thread_local std::string message;
	return message;
}

cg_status fail(cg_status status, const char *message) {
	last_error() = message;
	return status;
}

// the tables' strings, null-terminated, for the life of the library
struct TableText {
	std::vector<std::string> species_id, species_formula, reaction_id, reaction_equation;
};

const TableText &table_text() {
	static const TableText text = [] {
		static const char *const PHASE[] = { "s", "l", "g", "aq" };
		TableText t;
		const chem::SpeciesTable &table = chem::builtin_species();
		const auto key = [&table](chem::SpeciesId s) {
			const chem::SpeciesDef &def = table.species(s);
			return std::string(def.formula) + "(" + PHASE[static_cast<int>(def.phase)] + ")";
		};
		for (const chem::SpeciesDef &def : table.all_species()) {
			t.species_id.emplace_back(def.id);
			t.species_formula.emplace_back(def.formula);
		}
		for (const chem::Reaction &r : chem::builtin_reactions()) {
			t.reaction_id.emplace_back(r.id());
			std::string sides[2];
			for (const chem::Term &term : r.terms()) {
				std::string &side = sides[term.nu < 0 ? 0 : 1];
				const int count = term.nu < 0 ? -term.nu : term.nu;
				side += (side.empty() ? "" : " + ") + (count == 1 ? std::string() : std::to_string(count) + " ") + key(term.species);
			}
			t.reaction_equation.push_back(sides[0] + " -> " + sides[1]);
		}
		return t;
	}();
	return text;
}

} // namespace

extern "C" {

int32_t cg_api_version(void) {
	return CG_API_VERSION;
}

const char *cg_build_name(void) {
	thread_local std::string name;
	name = lab::build_name();
	return name.c_str();
}

const char *cg_last_error(void) {
	return last_error().c_str();
}

void cg_set_log(cg_log_fn fn, void *user) {
	if (fn == nullptr) {
		chem::set_error_sink(nullptr);
		return;
	}
	chem::set_error_sink([fn, user](const char *message) { fn(user, message); });
}

cg_status cg_debug_report(const char *message) {
	if (message == nullptr) {
		return fail(CG_ERR_INVALID_ARGUMENT, "cg_debug_report: null message");
	}
	chem::report_error(message);
	return fail(CG_ERR_INTERNAL, message);
}

int32_t cg_species_count(void) {
	return chem::builtin_species().species_count();
}

cg_status cg_species_get(int32_t index, cg_species_info *out) {
	if (out == nullptr || out->size < sizeof(cg_species_info)) {
		return fail(CG_ERR_INVALID_ARGUMENT, "cg_species_get: out is null or its size is not set");
	}
	const chem::SpeciesTable &table = chem::builtin_species();
	const chem::SpeciesId id{ index };
	if (!table.has(id)) {
		return fail(CG_ERR_INVALID_ARGUMENT, "cg_species_get: no such species");
	}
	const chem::SpeciesDef &def = table.species(id);
	const TableText &text = table_text();
	out->phase = static_cast<int32_t>(def.phase);
	out->charge = def.charge;
	out->id = text.species_id[static_cast<size_t>(index)].c_str();
	out->formula = text.species_formula[static_cast<size_t>(index)].c_str();
	out->molar_mass = static_cast<double>(table.molar_mass_ug(id)) * 1e-6;
	return CG_OK;
}

int32_t cg_reaction_count(void) {
	return static_cast<int32_t>(chem::builtin_reactions().size());
}

const char *cg_reaction_id(int32_t index) {
	if (index < 0 || index >= cg_reaction_count()) {
		fail(CG_ERR_INVALID_ARGUMENT, "cg_reaction_id: no such reaction");
		return nullptr;
	}
	return table_text().reaction_id[static_cast<size_t>(index)].c_str();
}

const char *cg_reaction_equation(int32_t index) {
	if (index < 0 || index >= cg_reaction_count()) {
		fail(CG_ERR_INVALID_ARGUMENT, "cg_reaction_equation: no such reaction");
		return nullptr;
	}
	return table_text().reaction_equation[static_cast<size_t>(index)].c_str();
}

cg_lab *cg_lab_open(const char *bench_json) {
	if (bench_json == nullptr) {
		fail(CG_ERR_INVALID_ARGUMENT, "cg_lab_open: null bench");
		return nullptr;
	}
	try {
		std::string why;
		std::unique_ptr<lab::Lab> made = lab::Lab::open(bench_json, why);
		if (!made) {
			fail(CG_ERR_INVALID_ARGUMENT, why.c_str());
			return nullptr;
		}
		return new cg_lab{ std::move(made), {} };
	} catch (const std::exception &e) {
		fail(CG_ERR_INTERNAL, e.what());
		return nullptr;
	}
}

void cg_lab_close(cg_lab *lab) {
	delete lab;
}

cg_status cg_lab_command(cg_lab *lab, const char *command_json) {
	if (lab == nullptr || command_json == nullptr) {
		return fail(CG_ERR_INVALID_ARGUMENT, "cg_lab_command: null argument");
	}
	try {
		std::string why;
		if (lab->lab->command(command_json, why)) {
			return CG_OK;
		}
		return fail(lab->lab->failed() ? CG_ERR_INVARIANT : CG_ERR_REFUSED, why.c_str());
	} catch (const std::exception &e) {
		return fail(CG_ERR_INTERNAL, e.what());
	}
}

cg_status cg_lab_advance(cg_lab *lab, int64_t steps) {
	if (lab == nullptr || steps < 0) {
		return fail(CG_ERR_INVALID_ARGUMENT, "cg_lab_advance: null lab or negative steps");
	}
	try {
		std::string why;
		if (lab->lab->advance(steps, why)) {
			return CG_OK;
		}
		return fail(CG_ERR_INVARIANT, why.c_str());
	} catch (const std::exception &e) {
		return fail(CG_ERR_INTERNAL, e.what());
	}
}

void cg_lab_set_checking(cg_lab *lab, int32_t on) {
	if (lab != nullptr) {
		lab->lab->set_checking(on != 0);
	}
}

int64_t cg_lab_step(const cg_lab *lab) {
	return lab != nullptr ? lab->lab->step() : 0;
}

const char *cg_lab_report(cg_lab *lab) {
	if (lab == nullptr) {
		fail(CG_ERR_INVALID_ARGUMENT, "cg_lab_report: null lab");
		return nullptr;
	}
	try {
		lab->text = lab->lab->report();
		return lab->text.c_str();
	} catch (const std::exception &e) {
		fail(CG_ERR_INTERNAL, e.what());
		return nullptr;
	}
}

const char *cg_lab_digest(cg_lab *lab) {
	if (lab == nullptr) {
		fail(CG_ERR_INVALID_ARGUMENT, "cg_lab_digest: null lab");
		return nullptr;
	}
	lab->text = lab->lab->digest();
	return lab->text.c_str();
}

const char *cg_lab_save(cg_lab *lab) {
	if (lab == nullptr) {
		fail(CG_ERR_INVALID_ARGUMENT, "cg_lab_save: null lab");
		return nullptr;
	}
	try {
		lab->text = lab->lab->save();
		return lab->text.c_str();
	} catch (const std::exception &e) {
		fail(CG_ERR_INTERNAL, e.what());
		return nullptr;
	}
}

cg_lab *cg_lab_load(const char *save_json) {
	if (save_json == nullptr) {
		fail(CG_ERR_INVALID_ARGUMENT, "cg_lab_load: null save");
		return nullptr;
	}
	try {
		std::string why;
		std::unique_ptr<lab::Lab> made = lab::Lab::load(save_json, why);
		if (!made) {
			fail(CG_ERR_INVALID_ARGUMENT, why.c_str());
			return nullptr;
		}
		return new cg_lab{ std::move(made), {} };
	} catch (const std::exception &e) {
		fail(CG_ERR_INTERNAL, e.what());
		return nullptr;
	}
}

cg_ui *cg_ui_open(const char *bench_json) {
	if (bench_json == nullptr) {
		fail(CG_ERR_INVALID_ARGUMENT, "cg_ui_open: null bench");
		return nullptr;
	}
	try {
		std::string why;
		std::unique_ptr<lab::Ui> made = lab::Ui::open(bench_json, why);
		if (!made) {
			fail(CG_ERR_INVALID_ARGUMENT, why.c_str());
			return nullptr;
		}
		return new cg_ui{ std::move(made), {}, false, {}, {}, {} };
	} catch (const std::exception &e) {
		fail(CG_ERR_INTERNAL, e.what());
		return nullptr;
	}
}

void cg_ui_close(cg_ui *ui) {
	delete ui;
}

void cg_ui_pointer(cg_ui *ui, float x, float y, int32_t down) { // down: the state
	if (ui == nullptr) {
		return;
	}
	try {
		const int mods = ((down & CG_POINTER_SHIFT) ? lab::Ui::ModShift : 0) | ((down & CG_POINTER_CTRL) ? lab::Ui::ModCtrl : 0) | ((down & CG_POINTER_META) ? lab::Ui::ModMeta : 0) |
				((down & CG_POINTER_RIGHT) ? lab::Ui::ModRight : 0);
		ui->ui->pointer(x, y, (down & CG_POINTER_DOWN) != 0, mods);
	} catch (const std::exception &e) {
		fail(CG_ERR_INTERNAL, e.what());
	}
}

void cg_ui_set_budget(cg_ui *ui, double ms) {
	if (ui != nullptr) {
		ui->ui->set_budget(ms);
	}
}

void cg_ui_frame(cg_ui *ui, double seconds) {
	if (ui == nullptr) {
		return;
	}
	try {
		if (ui->playing && !ui->play.next(*ui->ui)) {
			ui->playing = false;
		}
		ui->ui->frame(seconds);
	} catch (const std::exception &e) {
		fail(CG_ERR_INTERNAL, e.what());
	}
}

cg_status cg_ui_play(cg_ui *ui, const char *play_json) {
	if (ui == nullptr || play_json == nullptr) {
		return fail(CG_ERR_INVALID_ARGUMENT, "cg_ui_play: null argument");
	}
	try {
		std::string why;
		if (!lab::Play::read(play_json, ui->play, why)) {
			return fail(CG_ERR_INVALID_ARGUMENT, ("cg_ui_play: " + why).c_str());
		}
		ui->playing = true;
		return CG_OK;
	} catch (const std::exception &e) {
		return fail(CG_ERR_INTERNAL, e.what());
	}
}

const char *cg_ui_playing(cg_ui *ui) {
	return ui != nullptr && ui->playing ? ui->play.mark().c_str() : nullptr;
}

const char *cg_ui_play_shot(cg_ui *ui) {
	return ui != nullptr && ui->playing ? ui->play.shot().c_str() : nullptr;
}

static_assert(sizeof(cg_draw_image) == sizeof(lab::DrawImage) && offsetof(cg_draw_image, rgb) == offsetof(lab::DrawImage, rgb) && offsetof(cg_draw_image, hash) == offsetof(lab::DrawImage, hash),
		"a draw list's image is the screen's (Stage 61)");

cg_status cg_ui_draw(cg_ui *ui, cg_draw_list *out) {
	if (ui == nullptr || out == nullptr || out->size < sizeof(cg_draw_list)) {
		return fail(CG_ERR_INVALID_ARGUMENT, "cg_ui_draw: null argument, or the list's size is not set");
	}
	try {
		const lab::DrawList &list = ui->ui->draw();
		out->command_count = static_cast<int32_t>(list.commands.size());
		out->commands = reinterpret_cast<const cg_draw_command *>(list.commands.data());
		out->point_count = static_cast<int32_t>(list.points.size() / 2);
		out->points = list.points.data();
		out->text_size = static_cast<int32_t>(list.text.size());
		out->text = list.text.c_str();
		out->above = list.above;
		out->image_count = static_cast<int32_t>(list.images.size());
		out->images = reinterpret_cast<const cg_draw_image *>(list.images.data());
		return CG_OK;
	} catch (const std::exception &e) {
		return fail(CG_ERR_INTERNAL, e.what());
	}
}

namespace {

void fill_optics(const lab::OpticsScene &scene, cg_optics *out) {
	// (no view of a room unless the screen gives one: the scene as it is, still)
	const float whole[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	std::copy(whole, whole + 4, out->view);
	std::copy(whole, whole + 4, out->view_now);
	out->view_moving = 0;
	const lab::ViewRoom room = lab::Ui::view_room();
	out->room[0] = room.x;
	out->room[1] = room.y;
	out->room[2] = room.x + room.w;
	out->room[3] = room.y + room.h;
	out->relay_pool = scene.relay_pool;
	out->vec_count = static_cast<int32_t>(scene.table.size() / 4);
	out->table = scene.table.data();
	out->revision = scene.revision;
	out->row_count = static_cast<int32_t>(scene.rows.size());
	out->rows = scene.rows.data();
	out->line_count = static_cast<int32_t>(scene.lines.size() / 4);
	out->lines = scene.lines.data();
	out->light_count = static_cast<int32_t>(scene.lights.size() / 4);
	out->lights = scene.lights.data();
	out->wall_count = static_cast<int32_t>(scene.wall.size() / 4);
	out->wall = scene.wall.data();
	out->wall_revision = scene.wall_revision;
	out->rectangle_count = static_cast<int32_t>(scene.rectangles.size() / 4);
	out->rectangles = scene.rectangles.empty() ? nullptr : scene.rectangles.data();
	out->shape_count = static_cast<int32_t>(scene.shapes.size() / 4);
	out->shapes = scene.shapes.data();
	out->shapes_revision = scene.shapes_revision;
	out->image_count = static_cast<int32_t>(scene.images.size() / 4);
	out->images = scene.images.empty() ? nullptr : scene.images.data();
	out->images_revision = scene.images_revision;
}

void fill_draw(const lab::DrawList &list, cg_draw_list *out) {
	out->command_count = static_cast<int32_t>(list.commands.size());
	out->commands = reinterpret_cast<const cg_draw_command *>(list.commands.data());
	out->point_count = static_cast<int32_t>(list.points.size() / 2);
	out->points = list.points.data();
	out->text_size = static_cast<int32_t>(list.text.size());
	out->text = list.text.c_str();
	out->above = list.above;
	out->image_count = static_cast<int32_t>(list.images.size());
	out->images = reinterpret_cast<const cg_draw_image *>(list.images.data());
}

} // namespace

cg_demo *cg_demo_open(const char *name) {
	if (name == nullptr || std::string(name) != "prism") {
		last_error() = "cg_demo_open: the only demo is \"prism\"";
		return nullptr;
	}
	try {
		auto demo = std::make_unique<cg_demo>();
		demo->prism = std::make_unique<lab::PrismDemo>();
		return demo.release();
	} catch (const std::exception &e) {
		last_error() = e.what();
		return nullptr;
	}
}

void cg_demo_close(cg_demo *demo) {
	delete demo;
}

void cg_demo_pointer(cg_demo *demo, float x, float y, int32_t down) {
	if (demo != nullptr) {
		demo->prism->pointer(x, y, down != 0);
	}
}

void cg_demo_turn(cg_demo *demo, float radians) {
	if (demo != nullptr) {
		demo->prism->turn(radians);
	}
}

cg_status cg_demo_draw(cg_demo *demo, cg_draw_list *out) {
	if (demo == nullptr || out == nullptr || out->size < sizeof(cg_draw_list)) {
		return fail(CG_ERR_INVALID_ARGUMENT, "cg_demo_draw: null argument, or the list's size is not set");
	}
	try {
		fill_draw(demo->prism->draw(), out);
		return CG_OK;
	} catch (const std::exception &e) {
		return fail(CG_ERR_INTERNAL, e.what());
	}
}

cg_status cg_demo_optics(cg_demo *demo, cg_optics *out) {
	if (demo == nullptr || out == nullptr || out->size < sizeof(cg_optics)) {
		return fail(CG_ERR_INVALID_ARGUMENT, "cg_demo_optics: null argument, or the size is not set");
	}
	try {
		fill_optics(demo->prism->optics(), out);
		return CG_OK;
	} catch (const std::exception &e) {
		return fail(CG_ERR_INTERNAL, e.what());
	}
}

cg_status cg_ui_optics(cg_ui *ui, cg_optics *out) {
	if (ui == nullptr || out == nullptr || out->size < sizeof(cg_optics)) {
		return fail(CG_ERR_INVALID_ARGUMENT, "cg_ui_optics: null argument, or the size is not set");
	}
	try {
		fill_optics(ui->ui->optics(), out);
		// (the views as the scene was made, just now)
		lab::view_affine(ui->ui->scene_view(), lab::Ui::view_room(), out->view);
		lab::view_affine(ui->ui->view(), lab::Ui::view_room(), out->view_now);
		out->view_moving = ui->ui->view_moving() ? 1 : 0;
		return CG_OK;
	} catch (const std::exception &e) {
		return fail(CG_ERR_INTERNAL, e.what());
	}
}

cg_status cg_ui_optics_compare(cg_ui *ui, const uint8_t *rgba, int32_t width, int32_t height, cg_optics_parity *out) {
	if (ui == nullptr || rgba == nullptr || out == nullptr || out->size < sizeof(cg_optics_parity) || width <= 0 || height <= 0) {
		return fail(CG_ERR_INVALID_ARGUMENT, "cg_ui_optics_compare: null argument, no picture, or the size is not set");
	}
	try {
		const lab::OpticsParity parity = lab::optics_compare(ui->ui->optics(), rgba, width, height);
		out->worst = static_cast<int32_t>(std::ceil(parity.worst));
		out->covered = parity.covered;
		out->over = parity.over;
		out->worst_x = parity.worst_x;
		out->worst_y = parity.worst_y;
		return CG_OK;
	} catch (const std::exception &e) {
		return fail(CG_ERR_INTERNAL, e.what());
	}
}

void cg_ui_zoom(cg_ui *ui, float x, float y, float factor) {
	if (ui != nullptr) {
		ui->ui->zoom(x, y, factor);
	}
}

void cg_ui_pan(cg_ui *ui, float dx, float dy) {
	if (ui != nullptr) {
		ui->ui->pan(dx, dy);
	}
}

void cg_ui_view(cg_ui *ui, float view[3]) {
	if (ui == nullptr || view == nullptr) {
		return;
	}
	const lab::View v = ui->ui->view();
	view[0] = v.scale;
	view[1] = v.x;
	view[2] = v.y;
}

void cg_ui_key(cg_ui *ui, int32_t key, uint32_t codepoint) {
	if (ui == nullptr || key < CG_KEY_TEXT || key > CG_KEY_VIEW) {
		return;
	}
	try {
		ui->ui->key(static_cast<lab::Ui::Key>(key), codepoint);
	} catch (const std::exception &e) {
		fail(CG_ERR_INTERNAL, e.what());
	}
}

cg_status cg_ui_signals(cg_ui *ui, cg_signals *out) {
	if (ui == nullptr || out == nullptr || out->size < sizeof(cg_signals)) {
		return fail(CG_ERR_INVALID_ARGUMENT, "cg_ui_signals: null argument, or the size is not set");
	}
	const lab::LabView view = ui->ui->lab().view();
	ui->part_text.clear();
	ui->parts.clear();
	for (const lab::ItemView &v : view.items) {
		if (v.kind == "timer" || v.kind == "monitor" || v.kind == "alarm") {
			ui->part_text.push_back(v.id);
			ui->part_text.push_back(v.kind);
			cg_part p{};
			p.col = v.col;
			p.row = v.row;
			p.out = v.part.out ? 1 : 0;
			p.ringing = v.part.ringing ? 1 : 0;
			p.left_s = v.kind == "timer" ? static_cast<double>(v.part.left) / 60.0 : 0.0;
			ui->parts.push_back(p);
		}
	}
	for (size_t i = 0; i < ui->parts.size(); ++i) {
		ui->parts[i].id = ui->part_text[2 * i].c_str();
		ui->parts[i].kind = ui->part_text[2 * i + 1].c_str();
	}
	out->count = static_cast<int32_t>(ui->parts.size());
	out->parts = ui->parts.data();
	out->wires = static_cast<int32_t>(view.wires.size());
	out->rigs = static_cast<int32_t>(view.rigs.size());
	out->ringing = static_cast<int32_t>(view.ringing.size());
	out->paused = ui->ui->paused() ? 1 : 0;
	return CG_OK;
}

cg_status cg_ui_tutor(cg_ui *ui, const char *tutorial_json) {
	if (ui == nullptr || tutorial_json == nullptr) {
		return fail(CG_ERR_INVALID_ARGUMENT, "cg_ui_tutor: null argument");
	}
	try {
		std::string why;
		return ui->ui->tutor(tutorial_json, why) ? CG_OK : fail(CG_ERR_INVALID_ARGUMENT, why.c_str());
	} catch (const std::exception &e) {
		return fail(CG_ERR_INTERNAL, e.what());
	}
}

cg_status cg_ui_tutorial(cg_ui *ui, cg_tutorial *out) {
	// a caller built before Stage 41 has no `strict`: its size ends at `steps`
	if (ui == nullptr || out == nullptr || out->size < offsetof(cg_tutorial, strict)) {
		return fail(CG_ERR_INVALID_ARGUMENT, "cg_ui_tutorial: null argument, or the size is not set");
	}
	const lab::Tutorial *t = ui->ui->tutorial();
	out->state = t != nullptr ? static_cast<int32_t>(t->view().state) : CG_TUTORIAL_NONE;
	out->step = t != nullptr ? t->view().step : 0;
	out->steps = t != nullptr ? t->view().steps : 0;
	if (out->size >= offsetof(cg_tutorial, strict) + sizeof out->strict) {
		out->strict = ui->ui->strict() ? 1 : 0;
	}
	return CG_OK;
}

void cg_ui_set_strict(cg_ui *ui, int32_t strict) {
	if (ui != nullptr) {
		ui->ui->set_strict(strict != 0);
	}
}

cg_status cg_ui_add_pictures(cg_ui *ui, const char *pictures_json) {
	if (ui == nullptr || pictures_json == nullptr) {
		return fail(CG_ERR_INVALID_ARGUMENT, "cg_ui_add_pictures: null argument");
	}
	std::string why;
	if (!ui->ui->add_pictures(pictures_json, why)) {
		return fail(CG_ERR_INVALID_ARGUMENT, why.c_str());
	}
	return CG_OK;
}

const char *cg_ui_pictures(cg_ui *ui) {
	if (ui == nullptr) {
		fail(CG_ERR_INVALID_ARGUMENT, "cg_ui_pictures: null screen");
		return nullptr;
	}
	ui->text = ui->ui->pictures();
	return ui->text.c_str();
}

int32_t cg_ui_pictures_changed(const cg_ui *ui) {
	return ui != nullptr && ui->ui->pictures_changed() ? 1 : 0;
}

void cg_ui_set_capture_ahead(cg_ui *ui, int32_t on) {
	if (ui != nullptr) {
		ui->ui->set_capture_ahead(on != 0);
	}
}

void cg_ui_capture_pictures(cg_ui *ui) {
	if (ui != nullptr) {
		ui->ui->capture_pictures();
	}
}

const char *cg_ui_picture_keys(cg_ui *ui) {
	if (ui == nullptr) {
		fail(CG_ERR_INVALID_ARGUMENT, "cg_ui_picture_keys: null screen");
		return nullptr;
	}
	ui->text = ui->ui->picture_keys();
	return ui->text.c_str();
}

cg_status cg_ui_capture_picture(cg_ui *ui, const char *supply) {
	if (ui == nullptr || supply == nullptr) {
		return fail(CG_ERR_INVALID_ARGUMENT, "cg_ui_capture_picture: null argument");
	}
	if (!ui->ui->capture_picture(supply)) {
		return fail(CG_ERR_INVALID_ARGUMENT, (std::string("cg_ui_capture_picture: no supply ") + supply).c_str());
	}
	return CG_OK;
}

cg_status cg_ui_experiments(cg_ui *ui, const char *catalogue_json, cg_read_file read, void *user) {
	if (ui == nullptr || catalogue_json == nullptr) {
		return fail(CG_ERR_INVALID_ARGUMENT, "cg_ui_experiments: null argument");
	}
	try {
		std::string why;
		std::function<std::optional<std::string>(const std::string &)> reader;
		if (read != nullptr) {
			reader = [&](const std::string &path) -> std::optional<std::string> {
				const char *text = read(user, path.c_str());
				return text != nullptr ? std::optional<std::string>(text) : std::nullopt;
			};
		}
		return ui->ui->experiments(catalogue_json, why, reader) ? CG_OK : fail(CG_ERR_INVALID_ARGUMENT, why.c_str());
	} catch (const std::exception &e) {
		return fail(CG_ERR_INTERNAL, e.what());
	}
}

cg_status cg_ui_choose(cg_ui *ui, const char *id) {
	if (ui == nullptr || id == nullptr) {
		return fail(CG_ERR_INVALID_ARGUMENT, "cg_ui_choose: null argument");
	}
	try {
		std::string why;
		return ui->ui->choose(id, why) ? CG_OK : fail(CG_ERR_INVALID_ARGUMENT, why.c_str());
	} catch (const std::exception &e) {
		return fail(CG_ERR_INTERNAL, e.what());
	}
}

const char *cg_ui_experiment(cg_ui *ui) {
	if (ui == nullptr) {
		return "";
	}
	ui->text = ui->ui->experiment();
	return ui->text.c_str();
}

const char *cg_ui_progress(cg_ui *ui) {
	if (ui == nullptr) {
		return "";
	}
	ui->text = ui->ui->progress();
	return ui->text.c_str();
}

cg_status cg_ui_set_progress(cg_ui *ui, const char *progress_json) {
	if (ui == nullptr || progress_json == nullptr) {
		return fail(CG_ERR_INVALID_ARGUMENT, "cg_ui_set_progress: null argument");
	}
	try {
		std::string why;
		return ui->ui->set_progress(progress_json, why) ? CG_OK : fail(CG_ERR_INVALID_ARGUMENT, why.c_str());
	} catch (const std::exception &e) {
		return fail(CG_ERR_INTERNAL, e.what());
	}
}

const char *cg_ui_message(cg_ui *ui) {
	return ui != nullptr ? ui->ui->message().c_str() : "";
}

const char *cg_ui_take_export(cg_ui *ui) {
	if (ui == nullptr) {
		return "";
	}
	ui->text = ui->ui->take_export();
	return ui->text.c_str();
}

const char *cg_ui_report(cg_ui *ui) {
	if (ui == nullptr) {
		fail(CG_ERR_INVALID_ARGUMENT, "cg_ui_report: null ui");
		return nullptr;
	}
	try {
		ui->text = ui->ui->lab().report();
		return ui->text.c_str();
	} catch (const std::exception &e) {
		fail(CG_ERR_INTERNAL, e.what());
		return nullptr;
	}
}

int32_t cg_ui_locate(cg_ui *ui, const char *what, float *x, float *y) {
	if (ui == nullptr || what == nullptr || x == nullptr || y == nullptr) {
		return 0;
	}
	return ui->ui->locate(what, *x, *y) ? 1 : 0;
}

} // extern "C"
