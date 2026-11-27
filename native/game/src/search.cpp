// Search in Supplies and in Storage (GAME_SPEC §13.1, Stage 61): lab/search.hpp.
#include "lab/search.hpp"
#include "lab/conditions.hpp"

#include "chem/species.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace lab::search {

namespace {

std::string lower(std::string_view s) {
	std::string out(s);
	std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return out;
}

// a name, whole and by its words ("conical_flask": "conical_flask", "conical", "flask")
void add_name(std::vector<std::string> &out, std::string_view name) {
	const std::string whole = lower(name);
	if (whole.empty()) {
		return;
	}
	out.push_back(whole);
	std::string word;
	for (const char c : whole + " ") {
		if (std::isalnum(static_cast<unsigned char>(c)) || c == '.') {
			word += c;
		} else if (!word.empty()) {
			out.push_back(word);
			word.clear();
		}
	}
}

} // namespace

std::vector<std::string> keywords(const ItemView &v, std::string_view goal) {
	std::vector<std::string> out;
	add_name(out, v.id);
	add_name(out, v.label);
	if (v.kind == "thermal") {
		out.push_back("camera");
		out.push_back("thermal");
		out.push_back("infrared");
	} else if (v.kind == "viewer") {
		out.push_back("viewer");
		out.push_back("spectral");
	} else {
		add_name(out, v.kind);
	}
	if (!v.glassware.empty()) {
		add_name(out, v.glassware);
	}
	if (v.nominal_ml > 0.0) {
		const std::string ml = std::to_string(static_cast<long long>(std::lround(v.nominal_ml)));
		out.push_back(ml);
		out.push_back(ml + "ml");
	}
	bool holds = false;
	double gas = 0.0; // (Stage 65) its gas, mol: the room's carbon dioxide in it is its air's, a trace
	for (const SubstanceView &s : v.contents) {
		gas += !s.wall && s.phase == 'g' ? s.mol : 0.0;
	}
	for (const SubstanceView &s : v.contents) {
		if (s.wall) {
			add_name(out, s.species); // what it is made of
			continue;
		}
		const bool air = s.phase == 'g' && (s.species == "nitrogen" || s.species == "oxygen" || (s.species == "carbon_dioxide" && s.mol <= 1e-3 * gas));
		holds = holds || !air;
		add_name(out, s.species);
		add_name(out, s.formula);
		out.push_back(s.phase == 's' ? "solid" : s.phase == 'l' ? "liquid" : s.phase == 'g' ? "gas" : "dissolved");
		if (s.phase == 'a') {
			out.push_back("solution");
		}
	}
	// (Stage 63) what its dissolved ions make: a retired formula unit's name and formula (sulfuric acid, copper sulfate);
	// (Stage 76) of them those that are a share of it, a hundredth of the most it holds of one at least (a sulfate
	// whose metal gives up a hydrogen ion holds a trace of bisulfate, which is no sulfuric acid)
	const chem::SpeciesTable &table = chem::builtin_species();
	std::vector<std::pair<int, double>> units;
	double most = 0.0;
	for (int k = 0; k < table.species_count(); ++k) {
		const chem::SpeciesDef &d = table.species(chem::SpeciesId{ k });
		if (d.retired) {
			const double mol = mol_of(v.contents, std::string(d.id));
			if (mol > 1e-9) {
				units.emplace_back(k, mol);
				most = std::max(most, mol);
			}
		}
	}
	for (const auto &[k, mol] : units) {
		if (mol >= 0.01 * most) {
			const chem::SpeciesDef &d = table.species(chem::SpeciesId{ k });
			add_name(out, std::string(d.id));
			add_name(out, std::string(d.formula));
		}
	}
	if ((v.kind == "vessel" || v.kind == "balloon") && !holds) {
		out.push_back("empty");
	}
	if (!goal.empty()) {
		add_name(out, goal);
		out.push_back("goal");
	}
	if (v.place == "outbox" && v.slot > 0) {
		out.push_back(std::to_string(v.slot));
	}
	if (v.kind == "vessel" || v.kind == "balloon" || v.kind == "stopper") {
		if (v.celsius > 40.0) {
			out.push_back("hot");
		}
		if (v.celsius < 10.0) {
			out.push_back("cold");
		}
	}
	if (v.boiling) {
		out.push_back("boiling");
	}
	if (v.sealed) {
		out.push_back("sealed");
	}
	if (v.holds >= 0) {
		out.push_back("stoppered");
	}
	if (v.burst) {
		out.push_back("broken");
	}
	return out;
}

bool matches(std::string_view query, const std::vector<std::string> &keywords) {
	const std::string q = lower(query);
	std::string word;
	for (const char c : q + " ") {
		if (!std::isspace(static_cast<unsigned char>(c))) {
			word += c;
			continue;
		}
		if (word.empty()) {
			continue;
		}
		const bool found = std::any_of(keywords.begin(), keywords.end(), [&](const std::string &k) { return k.rfind(word, 0) == 0; });
		if (!found) {
			return false;
		}
		word.clear();
	}
	return true;
}

} // namespace lab::search
