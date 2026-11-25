#include "conditions_json.hpp"

#include "chem/species.hpp"

namespace lab {

bool CondReader::fail(const std::string &reason) {
	if (why.empty()) {
		why = reason;
	}
	return false;
}

bool CondReader::item(const json &j, const char *key, std::string &out) {
	if (!j.is_string()) {
		return fail(std::string("\"") + key + "\" is not an item's id");
	}
	out = j.get<std::string>();
	return item_of(bench, out) != nullptr || fail("the bench has no item \"" + out + "\"");
}

bool CondReader::number(const json &j, const char *key, std::optional<double> &out) {
	if (!j.is_number()) {
		return fail(std::string("\"") + key + "\" is not a number");
	}
	out = j.get<double>();
	return true;
}

bool CondReader::flag(const json &j, const char *key, std::optional<bool> &out) {
	if (!j.is_boolean()) {
		return fail(std::string("\"") + key + "\" is not true or false");
	}
	out = j.get<bool>();
	return true;
}

bool CondReader::species(const std::string &name) {
	return chem::builtin_species().has(chem::builtin_species().find(name)) || fail("no species \"" + name + "\"");
}

bool CondReader::part_pattern(const json &j, PartPattern &p) {
	if (!j.is_object()) {
		return fail("a part's pattern is an object");
	}
	for (const auto &[key, v] : j.items()) {
		bool ok = true;
		if (key == "id" || key == "kind" || key == "what" || key == "measure") {
			ok = v.is_string() || fail("\"" + key + "\" is not a name");
			if (ok) {
				(key == "id" ? p.id : key == "kind" ? p.kind : key == "what" ? p.what : p.measure) = v.get<std::string>();
			}
		} else if (key == "item") {
			ok = item(v, "item", p.item);
		} else if (key == "compare") {
			Comparison::Op op = Comparison::Below;
			ok = (v.is_string() && op_of(v.get<std::string>(), op)) || fail("no comparison " + v.dump());
			p.op = op;
		} else if (key == "value_at_least") {
			ok = number(v, "value_at_least", p.value_at_least);
		} else if (key == "value_at_most") {
			ok = number(v, "value_at_most", p.value_at_most);
		} else if (key == "watching") {
			ok = flag(v, "watching", p.watching);
		} else if (key == "signals") {
			ok = flag(v, "signals", p.signals);
		} else if (key == "ringing") {
			ok = flag(v, "ringing", p.ringing);
		} else if (key == "rang") {
			ok = flag(v, "rang", p.rang);
		} else {
			ok = fail("a part's pattern has no key \"" + key + "\"");
		}
		if (!ok) {
			return false;
		}
	}
	return true;
}

bool CondReader::rig_pattern(const json &j, RigPattern &p) {
	if (!j.is_object()) {
		return fail("a rig's pattern is an object");
	}
	for (const auto &[key, v] : j.items()) {
		bool ok = true;
		if (key == "from" || key == "control") {
			ok = v.is_string() || fail("\"" + key + "\" is not a name");
			if (ok) {
				(key == "from" ? p.from : p.control) = v.get<std::string>();
			}
		} else if (key == "item") {
			ok = v.is_string() || fail("\"item\" is not a name"); // equipment, or a timer made in play
			if (ok) {
				p.item = v.get<std::string>();
			}
		} else if (key == "on_at_least") {
			ok = number(v, "on_at_least", p.on_at_least);
		} else if (key == "on_at_most") {
			ok = number(v, "on_at_most", p.on_at_most);
		} else if (key == "off_at_least") {
			ok = number(v, "off_at_least", p.off_at_least);
		} else if (key == "off_at_most") {
			ok = number(v, "off_at_most", p.off_at_most);
		} else if (key == "on_leaves") {
			ok = flag(v, "on_leaves", p.on_leaves);
		} else if (key == "off_leaves") {
			ok = flag(v, "off_leaves", p.off_leaves);
		} else if (key == "change") {
			ok = flag(v, "change", p.change);
		} else {
			ok = fail("a rig's pattern has no key \"" + key + "\"");
		}
		if (!ok) {
			return false;
		}
	}
	return true;
}

bool CondReader::cond(const json &j, Cond &c) {
	if (!j.is_object() || j.empty()) {
		return fail("a condition is not an object with keys");
	}
	std::optional<std::string> compare;
	std::optional<double> value, value_b;
	for (const auto &[key, value_j] : j.items()) {
		const json &v = value_j;
		bool ok = true;
		if (extra_key && extra_key(key, v, c, *this, ok)) {
			// the reader's own
		} else if (key == "item") {
			ok = item(v, "item", c.item);
		} else if (key == "place") {
			ok = v.is_string() || fail("\"place\" is not a place");
			if (ok) {
				c.place = v.get<std::string>();
				ok = *c.place == "cabinet" || *c.place == "bench" || *c.place == "on" || *c.place == "under" || *c.place == "outbox" || fail("no place \"" + *c.place + "\"");
			}
		} else if (key == "on" || key == "under" || key == "drains_to") {
			std::string id;
			ok = item(v, key.c_str(), id);
			(key == "on" ? c.on : key == "under" ? c.under : c.drains_to) = id;
		} else if (key == "logging" || key == "recorded") { // (Stage 54) a quantity's name
			if (!v.is_string()) {
				ok = fail(std::string("\"") + key + "\" names a quantity, \"celsius\", \"kpa\"...");
			} else {
				(key == "logging" ? c.logging : c.recorded) = v.get<std::string>();
			}
		} else if (key == "noted") { // (Stage 55) a label written in the notebook
			if (!v.is_string()) {
				ok = fail("\"noted\" names a label written in the notebook");
			} else {
				c.noted = v.get<std::string>();
			}
		} else if (key == "noted_value") {
			if (!v.is_number()) {
				ok = fail("\"noted_value\" is a number");
			} else {
				c.noted_value = v.get<double>();
			}
		} else if (key == "submitted") {
			ok = flag(v, "submitted", c.submitted);
		} else if (key == "stirred") {
			ok = flag(v, "stirred", c.stirred);
		} else if (key == "burst") {
			ok = flag(v, "burst", c.burst);
		} else if (key == "conserved") {
			ok = flag(v, "conserved", c.conserved);
		} else if (key == "slot") {
			ok = (v.is_number_integer() && v.get<int>() >= 1 && v.get<int>() <= OUTBOX_SLOTS) || fail("\"slot\" is a slot of the outbox, 1 to 6");
			if (ok) {
				c.slot = v.get<int>();
			}
		} else if (key == "goals_met") {
			ok = flag(v, "goals_met", c.goals_met);
		} else if (key == "goal") {
			ok = v.is_string() || fail("\"goal\" names a goal");
			if (ok) {
				c.goal = v.get<std::string>();
				ok = std::any_of(bench.goals.begin(), bench.goals.end(), [&](const Goal &g) { return g.id == *c.goal; }) || fail("the bench has no goal \"" + *c.goal + "\"");
			}
		} else if (key == "broken") {
			ok = flag(v, "broken", c.broken);
		} else if (key == "valve_at_least") {
			ok = number(v, "valve_at_least", c.valve_at_least);
		} else if (key == "valve_at_most") {
			ok = number(v, "valve_at_most", c.valve_at_most);
		} else if (key == "above_c") {
			ok = number(v, "above_c", c.above_c);
		} else if (key == "below_c") {
			ok = number(v, "below_c", c.below_c);
		} else if (key == "liquid_above_ml") {
			ok = number(v, "liquid_above_ml", c.liquid_above_ml);
		} else if (key == "liquid_below_ml") {
			ok = number(v, "liquid_below_ml", c.liquid_below_ml);
		} else if (key == "above_g") {
			ok = number(v, "above_g", c.above_g);
		} else if (key == "below_g") {
			ok = number(v, "below_g", c.below_g);
		} else if (key == "purity_above") {
			ok = number(v, "purity_above", c.purity_above);
		} else if (key == "speed_at_least") {
			ok = number(v, "speed_at_least", c.speed_at_least);
		} else if (key == "species") {
			ok = v.is_string() || fail("\"species\" is not a species' id");
			if (ok) {
				c.species = v.get<std::string>();
				ok = species(c.species);
			}
		} else if (key == "holds_only") {
			ok = v.is_array() || fail("\"holds_only\" is not a list of species");
			if (ok) {
				c.holds_only.emplace();
				for (const json &s : v) {
					ok = ok && (s.is_string() || fail("\"holds_only\" is not a list of species")) && species(s.get<std::string>());
					if (ok) {
						c.holds_only->push_back(s.get<std::string>());
					}
				}
			}
		} else if (key == "quantity") {
			ok = (v.is_object() && v.contains("what") && v["what"].is_string()) || fail("\"quantity\" is not {\"what\", \"measure\"}");
			if (ok) {
				Quantity q;
				q.what = v["what"].get<std::string>();
				q.measure = v.value("measure", std::string("g"));
				if (v.contains("item")) {
					ok = item(v["item"], "item", q.item);
				}
				for (const auto &[k, unused] : v.items()) {
					(void)unused;
					ok = ok && ((k == "what" || k == "measure" || k == "item") || fail("a quantity has no key \"" + k + "\""));
				}
				std::string why_not;
				ok = ok && (valid(q, why_not) || fail(why_not));
				c.quantity = q;
			}
		} else if (key == "compare") {
			ok = v.is_string() || fail("\"compare\" is not a comparison's name");
			if (ok) {
				compare = v.get<std::string>();
			}
		} else if (key == "value") {
			ok = number(v, "value", value);
		} else if (key == "values") {
			ok = (v.is_array() && v.size() == 2 && v[0].is_number() && v[1].is_number()) || fail("\"values\" is not [low, high]");
			if (ok) {
				value = v[0].get<double>();
				value_b = v[1].get<double>();
			}
		} else if (key == "part") {
			ok = part_pattern(v, c.part.emplace());
		} else if (key == "wire") {
			ok = (v.is_object() && v.size() <= 2) || fail("a wire is {\"from\", \"to\"}: a part's id or kind each");
			if (ok) {
				WirePattern &w = c.wire.emplace();
				w.from = v.value("from", std::string());
				w.to = v.value("to", std::string());
			}
		} else if (key == "rig") {
			ok = rig_pattern(v, c.rig.emplace());
		} else if (key == "all" || key == "any") {
			ok = (v.is_array() && !v.empty()) || fail("\"" + key + "\" is not a list of conditions");
			for (size_t i = 0; ok && i < v.size(); ++i) {
				Cond inner;
				ok = cond(v[i], inner);
				(key == "all" ? c.all : c.any).push_back(std::move(inner));
			}
		} else if (key == "selected") {
			std::string id;
			ok = item(v, "selected", id);
			c.selected = id;
		} else if (key == "not") {
			Cond inner;
			ok = cond(v, inner);
			c.nots.push_back(std::move(inner));
		} else {
			ok = fail("a condition has no key \"" + key + "\"");
		}
		if (!ok) {
			return false;
		}
	}
	if (c.quantity || compare || value) {
		if (!c.quantity || !compare || !value) {
			return fail("a comparison is {\"quantity\", \"compare\", \"value\" or \"values\"}");
		}
		Comparison::Op op = Comparison::Below;
		if (!op_of(*compare, op)) {
			return fail("no comparison \"" + *compare + "\"");
		}
		if (two_values(op) != value_b.has_value()) {
			return fail(two_values(op) ? "\"" + *compare + "\" compares with \"values\": [low, high]" : "\"" + *compare + "\" compares with one \"value\"");
		}
		c.comparison = Comparison{ op, *value, value_b.value_or(0.0) };
		std::string why_not;
		if (!valid(c.comparison, why_not)) {
			return fail(why_not);
		}
		if (c.quantity->item.empty() && c.item.empty()) {
			return fail("a quantity names no \"item\"");
		}
	}
	const bool per_item = c.place || c.on || c.under || c.drains_to || c.submitted || c.slot || c.stirred || c.burst || c.valve_at_least || c.valve_at_most || c.above_c || c.below_c ||
			c.liquid_above_ml || c.liquid_below_ml || !c.species.empty() || c.holds_only;
	if (per_item && c.item.empty()) {
		return fail("a condition on an item names no \"item\"");
	}
	if (!c.species.empty() && !c.above_g && !c.below_g && !c.purity_above) {
		return fail("a condition names \"" + c.species + "\" but no \"above_g\", \"below_g\" or \"purity_above\"");
	}
	if ((c.above_g || c.below_g || c.purity_above) && c.species.empty()) {
		return fail("a condition on an amount names no \"species\"");
	}
	return true;
}

} // namespace lab
