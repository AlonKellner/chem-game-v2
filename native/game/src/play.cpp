#include "lab/play.hpp"

#include <nlohmann/json.hpp>

namespace lab {

bool Play::read(std::string_view text, Play &out, std::string &why) {
	using json = nlohmann::json;
	const json j = json::parse(text, nullptr, false);
	if (j.is_discarded() || !j.is_object() || j.value("format", "") != "chem-play/1" || !j.contains("steps") || !j["steps"].is_array()) {
		why = "not a \"chem-play/1\" with steps";
		return false;
	}
	Play play;
	for (const json &s : j["steps"]) {
		Step step;
		if (!s.is_object()) {
			why = "a step is not an object";
			return false;
		}
		for (const auto &[key, value] : s.items()) {
			if (key == "to" && value.is_string()) {
				step.to = value.get<std::string>();
			} else if (key == "to" && value.is_array() && value.size() == 2 && value[0].is_number() && value[1].is_number()) {
				step.placed = true;
				step.x = value[0].get<float>();
				step.y = value[1].get<float>();
			} else if (key == "frames" && value.is_number_integer() && value.get<int>() >= 1) {
				step.frames = value.get<int>();
			} else if (key == "button" && value.is_string()) {
				const std::string b = value.get<std::string>();
				step.button = b == "click" ? Click : (b == "right" ? Right : (b == "down" ? Down : (b == "up" ? Up : -1)));
				if (step.button < 0) {
					why = "a step's button is \"" + b + "\": click, right, down or up";
					return false;
				}
			} else if (key == "mark" && value.is_string()) {
				step.mark = value.get<std::string>();
			} else if (key == "do" && value.is_object()) {
				step.command = value.dump();
			} else if (key == "zoom" && value.is_number() && value.get<double>() > 0.0) {
				step.zoom = value.get<float>();
			} else if (key == "shot" && value.is_string()) {
				step.shot = value.get<std::string>();
			} else if (key == "type" && value.is_string()) {
				step.typed = value.get<std::string>(); // (Stage 58)
			} else {
				why = "a step's \"" + key + "\" is not read";
				return false;
			}
		}
		play.steps_.push_back(std::move(step));
	}
	out = std::move(play);
	return true;
}

bool Play::next(Ui &ui) {
	if (over()) {
		mark_.clear();
		return false;
	}
	const Step &step = steps_[at_];
	shot_.clear();
	if (frame_ == 0 && !step.command.empty()) {
		std::string why;
		if (!ui.lab().command(step.command, why) && refused_.empty()) {
			refused_ = step.command + ": " + why;
		}
	}
	if (frame_ == 0) {
		// where the step goes is looked up as it starts: the thing may have just come onto the screen
		from_x_ = to_x_ = x_;
		from_y_ = to_y_ = y_;
		if (step.placed) {
			to_x_ = step.x;
			to_y_ = step.y;
		} else if (!step.to.empty() && !ui.locate(step.to, to_x_, to_y_)) {
			to_x_ = x_;
			to_y_ = y_;
			if (missed_.empty()) {
				missed_ = step.to;
			}
		}
	}
	++frame_;
	const float f = static_cast<float>(frame_) / static_cast<float>(step.frames);
	x_ = from_x_ + (to_x_ - from_x_) * f;
	y_ = from_y_ + (to_y_ - from_y_) * f;
	mark_ = step.mark;
	if (step.zoom > 0.0f) {
		ui.zoom(x_, y_, step.zoom);
	}
	if (frame_ < step.frames) {
		ui.pointer(x_, y_, down_);
		return true;
	}
	shot_ = step.shot;
	switch (step.button) {
	case Click:
	case Right:
		ui.pointer(x_, y_, down_); // arrive, then press: a press where the pointer was not is a drag begun elsewhere
		ui.pointer(x_, y_, true, step.button == Right ? Ui::ModRight : 0);
		ui.pointer(x_, y_, false, step.button == Right ? Ui::ModRight : 0);
		down_ = false;
		break;
	case Down:
		ui.pointer(x_, y_, down_);
		down_ = true;
		ui.pointer(x_, y_, true);
		break;
	case Up:
		down_ = false;
		ui.pointer(x_, y_, false);
		break;
	default:
		ui.pointer(x_, y_, down_);
	}
	for (const char c : step.typed) {
		ui.key(Ui::KeyText, static_cast<uint32_t>(static_cast<unsigned char>(c)));
	}
	frame_ = 0;
	++at_;
	return true;
}

} // namespace lab
