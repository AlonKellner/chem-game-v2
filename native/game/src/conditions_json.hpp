#pragma once

#include "lab/conditions.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <string>

// Conditions read from JSON (GAME_SPEC §13.2), for the lab layer's own readers:
// tutorials, and the benches' goals. Private to native/game.
namespace lab {

struct CondReader {
	using json = nlohmann::json;

	const LabView &bench; // the items a condition may name
	std::string why; // the first reason a read failed
	// A reader's own keys: true when it took the key (`ok` then says whether it was read).
	std::function<bool(const std::string &key, const json &value, Cond &c, CondReader &r, bool &ok)> extra_key;

	bool fail(const std::string &reason);
	bool item(const json &j, const char *key, std::string &out);
	bool number(const json &j, const char *key, std::optional<double> &out);
	bool flag(const json &j, const char *key, std::optional<bool> &out);
	bool species(const std::string &name);
	bool cond(const json &j, Cond &c);
	bool part_pattern(const json &j, PartPattern &p);
	bool rig_pattern(const json &j, RigPattern &p);
};

} // namespace lab
