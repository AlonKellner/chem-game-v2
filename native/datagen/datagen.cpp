// chem_datagen: embeds the game's data in the core, and refuses data that
// breaks a conservation law (GAME_SPEC §4.3).
//
//   chem_datagen <data dir> <out dir>
//
// Reads elements.json, species.json and reactions.json. Derives each species'
// element counts from its formula, builds the tables through the core's own
// SpeciesTable::make and Reaction::make (so a reaction that does not balance in
// every element and in charge fails here, and the build with it), and writes
//   <out dir>/builtin_tables.cpp             the tables, with a static_assert
//   <out dir>/include/chem/builtin_ids.hpp   the species' and reactions' indices by name
#include "chem/species.hpp"
#include "formula.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using json = nlohmann::json;

[[noreturn]] void fail(const std::string &file, const std::string &message) {
	throw std::runtime_error(file + ": " + message);
}

json load(const std::filesystem::path &path, const char *format) {
	std::ifstream in(path);
	if (!in) {
		fail(path.string(), "cannot be read");
	}
	json j = json::parse(in, nullptr, true, true);
	if (!j.is_object() || j.value("format", "") != format) {
		fail(path.string(), std::string("\"format\" is not \"") + format + "\"");
	}
	return j;
}

// "1.008" g/mol -> 1008000 ug/mol, exactly (at most 6 decimals, never a float)
int64_t parse_mass(const std::string &text, const std::string &file) {
	int64_t whole = 0;
	int64_t fraction = 0;
	int decimals = 0;
	bool point = false;
	bool any = false;
	for (const char ch : text) {
		if (ch == '.' && !point) {
			point = true;
		} else if (ch >= '0' && ch <= '9') {
			any = true;
			if (point) {
				if (++decimals > 6) {
					fail(file, "mass \"" + text + "\" has more than 6 decimals");
				}
				fraction = fraction * 10 + (ch - '0');
			} else {
				whole = whole * 10 + (ch - '0');
				if (whole > 1000) {
					fail(file, "mass \"" + text + "\" is too large");
				}
			}
		} else {
			fail(file, "mass \"" + text + "\" is not a decimal number");
		}
	}
	if (!any) {
		fail(file, "an empty mass");
	}
	for (; decimals < 6; ++decimals) {
		fraction *= 10;
	}
	return whole * 1000000 + fraction;
}

bool is_identifier(const std::string &s) {
	if (s.empty() || !(s[0] >= 'a' && s[0] <= 'z')) {
		return false;
	}
	for (const char ch : s) {
		if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')) {
			return false;
		}
	}
	return true;
}

struct PhaseName {
	const char *text;
	chem::Phase phase;
	const char *cpp;
};
const PhaseName PHASES[] = {
	{ "s", chem::Phase::Solid, "Phase::Solid" },
	{ "l", chem::Phase::Liquid, "Phase::Liquid" },
	{ "g", chem::Phase::Gas, "Phase::Gas" },
	{ "aq", chem::Phase::Aqueous, "Phase::Aqueous" },
};

struct SpeciesRow {
	std::string id;
	std::string formula;
	const PhaseName *phase = nullptr;
	int charge = 0;
	chem::Atoms atoms{};
};

struct ReactionRow {
	std::string id;
	std::vector<chem::Term> terms;
};

std::string quoted(const std::string &s) {
	std::string out = "\"";
	for (const char ch : s) {
		if (ch == '"' || ch == '\\') {
			out += '\\';
		}
		out += ch;
	}
	return out + "\"";
}

int run(const std::filesystem::path &data, const std::filesystem::path &out) {
	// ---- elements
	const std::string elements_file = (data / "elements.json").string();
	const json elements_json = load(data / "elements.json", "chem-elements/1");
	std::vector<std::string> symbols;
	std::vector<int64_t> masses;
	for (const json &e : elements_json.at("elements")) {
		symbols.push_back(e.at("symbol").get<std::string>());
		masses.push_back(parse_mass(e.at("mass").get<std::string>(), elements_file));
	}
	if (symbols.size() > static_cast<size_t>(chem::MAX_ELEMENTS)) {
		fail(elements_file, "more elements than chem::MAX_ELEMENTS");
	}

	// ---- species
	const std::string species_file = (data / "species.json").string();
	const json species_json = load(data / "species.json", "chem-species/1");
	std::vector<SpeciesRow> species;
	std::map<std::string, int> by_key; // "H2O(g)" -> index
	for (const json &s : species_json.at("species")) {
		SpeciesRow row;
		row.id = s.at("id").get<std::string>();
		row.formula = s.at("formula").get<std::string>();
		row.charge = s.value("charge", 0);
		if (!is_identifier(row.id)) {
			fail(species_file, "species id \"" + row.id + "\" is not a lower-case identifier");
		}
		const std::string phase = s.at("phase").get<std::string>();
		for (const PhaseName &p : PHASES) {
			if (phase == p.text) {
				row.phase = &p;
			}
		}
		if (row.phase == nullptr) {
			fail(species_file, "species " + row.id + ": phase \"" + phase + "\" is not s, l, g or aq");
		}
		chemdata::AtomCounts counts;
		std::string why;
		if (!chemdata::parse_formula(row.formula, counts, why)) {
			fail(species_file, "species " + row.id + ": formula \"" + row.formula + "\": " + why);
		}
		for (const auto &[symbol, n] : counts) {
			size_t e = 0;
			while (e < symbols.size() && symbols[e] != symbol) {
				++e;
			}
			if (e == symbols.size()) {
				fail(species_file, "species " + row.id + ": element " + symbol + " is not in elements.json");
			}
			row.atoms[e] = n;
		}
		const std::string key = row.formula + "(" + phase + ")";
		if (!by_key.emplace(key, static_cast<int>(species.size())).second) {
			fail(species_file, "species " + key + " is listed twice");
		}
		species.push_back(row);
	}

	// ---- the table, through the core's own validation
	std::vector<chem::ElementDef> element_defs;
	for (size_t i = 0; i < symbols.size(); ++i) {
		element_defs.push_back(chem::ElementDef{ symbols[i], masses[i] });
	}
	std::vector<chem::SpeciesDef> species_defs;
	for (const SpeciesRow &row : species) {
		species_defs.push_back(chem::SpeciesDef{ row.id, row.formula, row.phase->phase, row.charge, row.atoms });
	}
	std::string why;
	const std::optional<chem::SpeciesTable> table = chem::SpeciesTable::make(element_defs, species_defs, &why);
	if (!table) {
		fail(species_file, why);
	}

	// ---- reactions: each must balance, or there is no build
	const std::string reactions_file = (data / "reactions.json").string();
	const json reactions_json = load(data / "reactions.json", "chem-reactions/1");
	std::vector<ReactionRow> reactions;
	for (const json &r : reactions_json.at("reactions")) {
		ReactionRow row;
		row.id = r.at("id").get<std::string>();
		if (!is_identifier(row.id)) {
			fail(reactions_file, "reaction id \"" + row.id + "\" is not a lower-case identifier");
		}
		for (const ReactionRow &other : reactions) {
			if (other.id == row.id) {
				fail(reactions_file, "reaction " + row.id + " is listed twice");
			}
		}
		const std::string equation = r.at("equation").get<std::string>();
		std::vector<chemdata::EquationTerm> terms;
		if (!chemdata::parse_equation(equation, terms, why)) {
			fail(reactions_file, "reaction " + row.id + ": \"" + equation + "\": " + why);
		}
		for (const chemdata::EquationTerm &t : terms) {
			const auto found = by_key.find(t.species);
			if (found == by_key.end()) {
				fail(reactions_file, "reaction " + row.id + ": species " + t.species + " is not in species.json");
			}
			row.terms.push_back(chem::Term{ chem::SpeciesId{ found->second }, t.nu });
		}
		if (!chem::Reaction::make(*table, row.id, row.terms, &why)) {
			fail(reactions_file, "\"" + equation + "\": " + why);
		}
		reactions.push_back(row);
	}

	// ---- write
	std::filesystem::create_directories(out / "include" / "chem");
	std::ostringstream cpp;
	cpp << "// Generated by chem_datagen from data/elements.json, data/species.json and\n"
		<< "// data/reactions.json. Do not edit.\n"
		<< "#include \"chem/species.hpp\"\n\n#include <cstdio>\n#include <cstdlib>\n#include <string>\n#include <vector>\n\n"
		<< "namespace chem {\nnamespace {\n\n"
		<< "constexpr ElementDef ELEMENTS[] = {\n";
	for (size_t i = 0; i < symbols.size(); ++i) {
		cpp << "\t{ " << quoted(symbols[i]) << ", " << masses[i] << " },\n";
	}
	cpp << "};\n\nconstexpr SpeciesDef SPECIES[] = {\n";
	for (const SpeciesRow &row : species) {
		cpp << "\t{ " << quoted(row.id) << ", " << quoted(row.formula) << ", " << row.phase->cpp << ", " << row.charge << ", Atoms{ { ";
		for (size_t e = 0; e < symbols.size(); ++e) {
			cpp << row.atoms[e] << (e + 1 < symbols.size() ? ", " : "");
		}
		cpp << " } } },\n";
	}
	cpp << "};\n\nconstexpr ReactionDef REACTIONS[] = {\n";
	for (const ReactionRow &row : reactions) {
		cpp << "\t{ " << quoted(row.id) << ", { { ";
		for (size_t i = 0; i < row.terms.size(); ++i) {
			cpp << "{ SpeciesId{ " << row.terms[i].species.index << " }, " << row.terms[i].nu << " }" << (i + 1 < row.terms.size() ? ", " : "");
		}
		cpp << " } }, " << row.terms.size() << " },\n";
	}
	cpp << "};\n\n"
		<< "// The compiler's own proof, independent of the generator's: no build holds an\n"
		<< "// unbalanced reaction (GAME_SPEC §4.3).\n"
		<< "static_assert(all_balanced(SPECIES, REACTIONS), \"data/reactions.json: a reaction does not balance in every element and in charge\");\n\n"
		<< "[[noreturn]] void broken(const std::string &why) {\n"
		<< "\tstd::fprintf(stderr, \"the built-in tables do not load: %s\\n\", why.c_str());\n"
		<< "\tstd::abort();\n}\n\n"
		<< "} // namespace\n\n"
		<< "const SpeciesTable &builtin_species() {\n"
		<< "\tstatic const SpeciesTable table = [] {\n"
		<< "\t\tstd::string why;\n"
		<< "\t\tstd::optional<SpeciesTable> made = SpeciesTable::make(std::vector<ElementDef>(std::begin(ELEMENTS), std::end(ELEMENTS)),\n"
		<< "\t\t\t\tstd::vector<SpeciesDef>(std::begin(SPECIES), std::end(SPECIES)), &why);\n"
		<< "\t\tif (!made) {\n\t\t\tbroken(why);\n\t\t}\n"
		<< "\t\treturn *made;\n\t}();\n\treturn table;\n}\n\n"
		<< "std::span<const Reaction> builtin_reactions() {\n"
		<< "\tstatic const std::vector<Reaction> reactions = [] {\n"
		<< "\t\tstd::vector<Reaction> made;\n"
		<< "\t\tfor (const ReactionDef &def : REACTIONS) {\n"
		<< "\t\t\tstd::string why;\n"
		<< "\t\t\tstd::optional<Reaction> r = Reaction::make(builtin_species(), def.id,\n"
		<< "\t\t\t\t\tstd::span<const Term>(def.terms.data(), static_cast<size_t>(def.term_count)), &why);\n"
		<< "\t\t\tif (!r) {\n\t\t\t\tbroken(why);\n\t\t\t}\n"
		<< "\t\t\tmade.push_back(*r);\n\t\t}\n"
		<< "\t\treturn made;\n\t}();\n\treturn reactions;\n}\n\n"
		<< "} // namespace chem\n";

	std::ostringstream hpp;
	hpp << "// Generated by chem_datagen from data/species.json and data/reactions.json.\n"
		<< "// Do not edit.\n#pragma once\n\n#include \"chem/species.hpp\"\n\n"
		<< "// The built-in species' ids and the built-in reactions' indices, by name.\n"
		<< "namespace chem::builtin {\n\n";
	for (size_t i = 0; i < species.size(); ++i) {
		hpp << "inline constexpr SpeciesId " << species[i].id << "{ " << i << " }; // " << species[i].formula << "(" << species[i].phase->text << ")\n";
	}
	hpp << "inline constexpr int SPECIES_COUNT = " << species.size() << ";\n\nnamespace reaction {\n";
	for (size_t i = 0; i < reactions.size(); ++i) {
		hpp << "inline constexpr int " << reactions[i].id << " = " << i << ";\n";
	}
	hpp << "inline constexpr int COUNT = " << reactions.size() << ";\n} // namespace reaction\n\n} // namespace chem::builtin\n";

	std::ofstream(out / "builtin_tables.cpp") << cpp.str();
	std::ofstream(out / "include" / "chem" / "builtin_ids.hpp") << hpp.str();
	return 0;
}

} // namespace

int main(int argc, char **argv) {
	if (argc != 3) {
		std::fprintf(stderr, "usage: chem_datagen <data dir> <out dir>\n");
		return 2;
	}
	try {
		return run(argv[1], argv[2]);
	} catch (const std::exception &e) {
		std::fprintf(stderr, "chem_datagen: %s\n", e.what());
		return 1;
	}
}
