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

#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <tuple>
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
	bool has_thermo = false;
	double hf = 0.0, cp = 0.0, s0 = 0.0; // J/mol, J/mol/K, J/mol/K
	bool wall = false;
	double density = 0.0; // g/cm^3; 0: none given
	double molar_volume = 0.0; // m^3/mol, derived (a dissolved species: from volume_cm3)
	double grain = 0.0; // m: a precipitate's grains (grain_um)
	std::vector<std::pair<int, int>> ions; // (ion, count): GAME_SPEC §4.6
};

// An ion as species.json names it: its formula, then its charge, "SO4-2".
struct IonRow {
	std::string name;
	int charge = 0;
	chem::Atoms atoms{};
};

struct ReactionRow {
	std::string id;
	std::vector<chem::Term> terms;
	chem::Kinetics kinetics{};
	int notice_level = 0; // GAME_SPEC §13.4: detail, event, warning, danger
	std::string notice_says;
	std::string explain; // GAME_SPEC §13.6: why it happens
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
	std::string why;
	const std::string species_file = (data / "species.json").string();
	const json species_json = load(data / "species.json", "chem-species/1");
	std::vector<SpeciesRow> species;
	std::vector<IonRow> ions;
	std::map<std::string, int> by_key; // "H2O(g)" -> index
	for (const json &s : species_json.at("species")) {
		SpeciesRow row;
		row.id = s.at("id").get<std::string>();
		row.formula = s.at("formula").get<std::string>();
		row.charge = s.value("charge", 0);
		if (s.contains("thermo")) {
			const json &t = s.at("thermo");
			row.has_thermo = true;
			row.hf = t.at("hf_kj").get<double>() * 1000.0;
			row.cp = t.at("cp").get<double>();
			row.s0 = t.at("s0").get<double>();
		}
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
		row.wall = s.value("wall", false);
		row.density = s.value("density", 0.0);
		if (row.density < 0.0 || (row.density > 0.0 && (row.phase->phase == chem::Phase::Gas || row.phase->phase == chem::Phase::Aqueous))) {
			fail(species_file, "species " + row.id + ": a density is positive, and only a solid or a liquid has one");
		}
		chemdata::AtomCounts counts;
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
		// what it is made of in solution (GAME_SPEC §4.6): its ions' elements and
		// charges sum to its own
		if (s.contains("ions")) {
			if (row.phase->phase != chem::Phase::Aqueous) {
				fail(species_file, "species " + row.id + ": only a dissolved species has ions");
			}
			chem::Atoms sum{};
			int charge = 0;
			for (const auto &[name, count_json] : s.at("ions").items()) {
				const int count = count_json.get<int>();
				const size_t sign = name.find_last_of("+-");
				if (sign == std::string::npos || sign == 0 || sign + 1 >= name.size() || count <= 0) {
					fail(species_file, "species " + row.id + ": ion \"" + name + "\" is not a formula, a sign and a charge, with a positive count");
				}
				int index = -1;
				for (size_t i = 0; i < ions.size(); ++i) {
					index = ions[i].name == name ? static_cast<int>(i) : index;
				}
				if (index < 0) {
					IonRow ion;
					ion.name = name;
					ion.charge = std::stoi(name.substr(sign + 1)) * (name[sign] == '-' ? -1 : 1);
					chemdata::AtomCounts ion_counts;
					if (!chemdata::parse_formula(name.substr(0, sign), ion_counts, why)) {
						fail(species_file, "species " + row.id + ": ion \"" + name + "\": " + why);
					}
					for (const auto &[symbol, n] : ion_counts) {
						size_t e = 0;
						while (e < symbols.size() && symbols[e] != symbol) {
							++e;
						}
						if (e == symbols.size()) {
							fail(species_file, "species " + row.id + ": ion " + name + ": element " + symbol + " is not in elements.json");
						}
						ion.atoms[e] = n;
					}
					index = static_cast<int>(ions.size());
					ions.push_back(ion);
				}
				for (size_t e = 0; e < sum.size(); ++e) {
					sum[e] += count * ions[static_cast<size_t>(index)].atoms[e];
				}
				charge += count * ions[static_cast<size_t>(index)].charge;
				row.ions.emplace_back(index, count);
			}
			if (sum != row.atoms || charge != row.charge || row.ions.size() > static_cast<size_t>(chem::MAX_IONS)) {
				fail(species_file, "species " + row.id + ": its ions do not sum to its formula and charge (at most " + std::to_string(chem::MAX_IONS) + " ions)");
			}
		}
		if (s.contains("grain_um")) {
			const double g = s.at("grain_um").get<double>();
			if (row.phase->phase != chem::Phase::Solid || row.wall || !(g > 0.0)) {
				fail(species_file, "species " + row.id + ": grain_um is a solid's positive grain diameter as it comes out of a solution");
			}
			row.grain = g * 1e-6;
		}
		if (s.contains("volume_cm3")) {
			const double v = s.at("volume_cm3").get<double>();
			if (row.phase->phase != chem::Phase::Aqueous || !(v > 0.0)) {
				fail(species_file, "species " + row.id + ": volume_cm3 is a dissolved species' positive apparent molar volume");
			}
			row.molar_volume = v * 1e-6;
		}
		if (row.density > 0.0) {
			// molar mass (ug/mol -> g/mol) over density (g/cm^3), in m^3/mol
			int64_t mass_ug = 0;
			for (size_t e = 0; e < symbols.size(); ++e) {
				mass_ug += row.atoms[e] * masses[e];
			}
			row.molar_volume = static_cast<double>(mass_ug) * 1e-6 / row.density * 1e-6;
		}
		const std::string key = row.formula + "(" + phase + ")";
		if (!row.wall && !by_key.emplace(key, static_cast<int>(species.size())).second) {
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
		chem::SpeciesDef def{ row.id, row.formula, row.phase->phase, row.charge, row.atoms, row.has_thermo, row.hf, row.cp, row.s0, row.wall, row.molar_volume };
		for (size_t i = 0; i < row.ions.size(); ++i) {
			def.ions[i] = chem::IonCount{ row.ions[i].first, row.ions[i].second };
		}
		def.grain = row.grain;
		species_defs.push_back(def);
	}

	// ---- the solution model (GAME_SPEC §10): optional; without it, no pairs
	const std::string solutions_file = (data / "solutions.json").string();
	chem::SolutionDef solution;
	for (const IonRow &ion : ions) {
		solution.ions.push_back(chem::IonDef{ ion.name, ion.charge });
	}
	if (std::filesystem::exists(data / "solutions.json")) {
		const json solutions_json = load(data / "solutions.json", "chem-solutions/1");
		solution.a_phi = solutions_json.at("a_phi").get<double>();
		solution.b = solutions_json.at("b").get<double>();
		// each ion's diffusion coefficient at 25 C, and the solvent's viscosity (GAME_SPEC §7)
		if (solutions_json.contains("diffusion")) {
			for (const auto &[name, value] : solutions_json.at("diffusion").items()) {
				if (name == "note" || name == "source") {
					continue;
				}
				bool found = false;
				for (chem::IonDef &ion : solution.ions) {
					if (ion.name == name) {
						ion.diffusion = value.get<double>();
						found = true;
					}
				}
				if (!found) {
					fail(solutions_file, "a diffusion coefficient names ion " + name + ", which no species is made of");
				}
			}
		}
		if (solutions_json.contains("viscosity")) {
			for (const json &v : solutions_json.at("viscosity")) {
				solution.viscosity.push_back(chem::ViscosityPoint{ v.at("celsius").get<double>() + 273.15, v.at("mpa_s").get<double>() / 1000.0 });
			}
		}
		// water's own ionisation (GAME_SPEC §9, Stage 43)
		if (solutions_json.contains("water_ionisation")) {
			const json &w = solutions_json.at("water_ionisation");
			solution.ionisation_h = w.at("h_kj").get<double>() * 1000.0;
			solution.ionisation_g = w.at("g_kj").get<double>() * 1000.0;
			solution.ionisation_cp = w.at("cp").get<double>();
			if (!(solution.ionisation_g > 0.0)) {
				fail(solutions_file, "water_ionisation: its Gibbs energy is positive: water is mostly not ionised");
			}
		}
		for (const json &p : solutions_json.at("pairs")) {
			const auto ion_of = [&](const char *key) {
				const std::string name = p.at(key).get<std::string>();
				for (size_t i = 0; i < ions.size(); ++i) {
					if (ions[i].name == name) {
						return static_cast<int32_t>(i);
					}
				}
				fail(solutions_file, "a pair names ion " + name + ", which no species is made of");
			};
			solution.pairs.push_back(chem::PairDef{ ion_of("cation"), ion_of("anion"), p.at("beta0").get<double>(), p.at("beta1").get<double>(),
					p.value("beta2", 0.0), p.at("c_phi").get<double>(), p.at("alpha1").get<double>(), p.value("alpha2", 12.0) });
		}
	}
	const std::optional<chem::SpeciesTable> table = chem::SpeciesTable::make(element_defs, species_defs, &why, solution);
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
		if (r.contains("kinetics")) {
			const json &k = r.at("kinetics");
			const std::string law = k.at("law").get<std::string>();
			if (law == "equilibrium") {
				row.kinetics.law = chem::Kinetics::Law::Equilibrium;
			} else if (law == "first_order") {
				const auto found = by_key.find(k.at("in").get<std::string>());
				if (found == by_key.end()) {
					fail(reactions_file, "reaction " + row.id + ": kinetics names a species that is not in species.json");
				}
				row.kinetics.law = chem::Kinetics::Law::FirstOrder;
				row.kinetics.in = chem::SpeciesId{ found->second };
				row.kinetics.prefactor = k.at("prefactor").get<double>();
				row.kinetics.activation = k.at("activation_kj").get<double>() * 1000.0;
			} else if (law == "surface") {
				const auto found = by_key.find(k.at("in").get<std::string>());
				if (found == by_key.end()) {
					fail(reactions_file, "reaction " + row.id + ": kinetics names a species that is not in species.json");
				}
				row.kinetics.law = chem::Kinetics::Law::Surface;
				row.kinetics.in = chem::SpeciesId{ found->second };
				row.kinetics.prefactor = k.at("prefactor").get<double>();
				row.kinetics.activation = k.at("activation_kj").get<double>() * 1000.0;
				if (k.contains("driver")) {
					const auto driver = by_key.find(k.at("driver").get<std::string>());
					if (driver == by_key.end()) {
						fail(reactions_file, "reaction " + row.id + ": kinetics names a driver that is not in species.json");
					}
					row.kinetics.driver = chem::SpeciesId{ driver->second };
				}
				row.kinetics.nucleation = k.value("nucleation", 0.0);
				row.kinetics.nucleation_order = k.value("nucleation_order", 0.0);
				row.kinetics.driver_order = k.value("driver_order", 1.0);
				if (!(row.kinetics.driver_order > 0.0) || (k.contains("driver_order") && !k.contains("driver"))) {
					fail(reactions_file, "reaction " + row.id + ": a driver's order is positive, and only a reaction with a driver has one");
				}
			} else {
				fail(reactions_file, "reaction " + row.id + ": a kinetics law is first_order, surface or equilibrium");
			}
		}
		if (r.contains("notice")) {
			const json &n = r.at("notice");
			const std::string level = n.value("level", std::string("detail"));
			const char *const LEVELS[] = { "detail", "event", "warning", "danger" };
			row.notice_level = -1;
			for (int k = 0; k < 4; ++k) {
				row.notice_level = level == LEVELS[k] ? k : row.notice_level;
			}
			if (row.notice_level < 0) {
				fail(reactions_file, "reaction " + row.id + ": a notice's level is detail, event, warning or danger");
			}
			row.notice_says = n.value("says", std::string());
		}
		row.explain = r.value("explain", std::string());
		if (!chem::Reaction::make(*table, row.id, row.terms, &why, row.kinetics)) {
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
		// 17 significant digits: the double the data file's number rounds to, exactly
		char thermo[200];
		std::snprintf(thermo, sizeof thermo, ", %s, %.17g, %.17g, %.17g, %s, %.17g", row.has_thermo ? "true" : "false", row.hf, row.cp, row.s0,
				row.wall ? "true" : "false", row.molar_volume);
		cpp << " } }" << thermo << ", { {";
		for (size_t i = 0; i < static_cast<size_t>(chem::MAX_IONS); ++i) {
			cpp << (i ? ", " : " ") << "IonCount{ " << (i < row.ions.size() ? row.ions[i].first : -1) << ", " << (i < row.ions.size() ? row.ions[i].second : 0) << " }";
		}
		char grain[40];
		std::snprintf(grain, sizeof grain, "%.17g", row.grain);
		cpp << " } }, " << grain << " },\n";
	}
	cpp << "};\n\n// the solution model (GAME_SPEC §10): data/solutions.json\nconstexpr IonDef IONS[] = {\n";
	for (const chem::IonDef &ion : solution.ions) {
		char diffusion[40];
		std::snprintf(diffusion, sizeof diffusion, "%.17g", ion.diffusion);
		cpp << "\t{ " << quoted(std::string(ion.name)) << ", " << ion.charge << ", " << diffusion << " },\n";
	}
	if (ions.empty()) {
		cpp << "\t{ \"none\", 0, 0.0 },\n";
	}
	cpp << "};\n\nconstexpr ViscosityPoint VISCOSITY[] = {\n";
	for (const chem::ViscosityPoint &v : solution.viscosity) {
		char point[80];
		std::snprintf(point, sizeof point, "\t{ %.17g, %.17g },\n", v.kelvin, v.pascal_seconds);
		cpp << point;
	}
	if (solution.viscosity.empty()) {
		cpp << "\t{},\n";
	}
	cpp << "};\n\nconstexpr PairDef PAIRS[] = {\n";
	for (const chem::PairDef &p : solution.pairs) {
		char pair[300];
		std::snprintf(pair, sizeof pair, "\t{ %d, %d, %.17g, %.17g, %.17g, %.17g, %.17g, %.17g },\n", p.cation, p.anion, p.beta0, p.beta1, p.beta2, p.c_phi, p.alpha1,
				p.alpha2);
		cpp << pair;
	}
	if (solution.pairs.empty()) {
		cpp << "\t{},\n";
	}
	char model[240];
	std::snprintf(model, sizeof model, "constexpr double A_PHI = %.17g, DEBYE_B = %.17g, IONISATION_H = %.17g, IONISATION_G = %.17g, IONISATION_CP = %.17g;\n", solution.a_phi,
			solution.b, solution.ionisation_h, solution.ionisation_g, solution.ionisation_cp);
	cpp << "};\n" << model << "constexpr size_t ION_COUNT = " << ions.size() << ", PAIR_COUNT = " << solution.pairs.size() << ", VISCOSITY_COUNT = " << solution.viscosity.size()
		<< ";\n";
	cpp << "\nconstexpr ReactionDef REACTIONS[] = {\n";
	for (const ReactionRow &row : reactions) {
		cpp << "\t{ " << quoted(row.id) << ", { { ";
		for (size_t i = 0; i < row.terms.size(); ++i) {
			cpp << "{ SpeciesId{ " << row.terms[i].species.index << " }, " << row.terms[i].nu << " }" << (i + 1 < row.terms.size() ? ", " : "");
		}
		char kinetics[360];
		std::snprintf(kinetics, sizeof kinetics, ", Kinetics{ %s, SpeciesId{ %d }, %.17g, %.17g, SpeciesId{ %d }, %.17g, %.17g, %.17g }",
				row.kinetics.law == chem::Kinetics::Law::FirstOrder ? "Kinetics::Law::FirstOrder"
						: row.kinetics.law == chem::Kinetics::Law::Equilibrium ? "Kinetics::Law::Equilibrium"
						: row.kinetics.law == chem::Kinetics::Law::Surface ? "Kinetics::Law::Surface" : "Kinetics::Law::None",
				row.kinetics.in.index, row.kinetics.prefactor, row.kinetics.activation, row.kinetics.driver.index, row.kinetics.nucleation,
				row.kinetics.nucleation_order, row.kinetics.driver_order);
		cpp << " } }, " << row.terms.size() << kinetics << " },\n";
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
		<< "\t\tSolutionDef solution{ A_PHI, DEBYE_B, std::vector<IonDef>(IONS, IONS + ION_COUNT), std::vector<PairDef>(PAIRS, PAIRS + PAIR_COUNT),\n"
		<< "\t\t\t\tstd::vector<ViscosityPoint>(VISCOSITY, VISCOSITY + VISCOSITY_COUNT), IONISATION_H, IONISATION_G, IONISATION_CP };\n"
		<< "\t\tstd::optional<SpeciesTable> made = SpeciesTable::make(std::vector<ElementDef>(std::begin(ELEMENTS), std::end(ELEMENTS)),\n"
		<< "\t\t\t\tstd::vector<SpeciesDef>(std::begin(SPECIES), std::end(SPECIES)), &why, std::move(solution));\n"
		<< "\t\tif (!made) {\n\t\t\tbroken(why);\n\t\t}\n"
		<< "\t\treturn *made;\n\t}();\n\treturn table;\n}\n\n"
		<< "std::span<const Reaction> builtin_reactions() {\n"
		<< "\tstatic const std::vector<Reaction> reactions = [] {\n"
		<< "\t\tstd::vector<Reaction> made;\n"
		<< "\t\tfor (const ReactionDef &def : REACTIONS) {\n"
		<< "\t\t\tstd::string why;\n"
		<< "\t\t\tstd::optional<Reaction> r = Reaction::make(builtin_species(), def.id,\n"
		<< "\t\t\t\t\tstd::span<const Term>(def.terms.data(), static_cast<size_t>(def.term_count)), &why, def.kinetics);\n"
		<< "\t\t\tif (!r) {\n\t\t\t\tbroken(why);\n\t\t\t}\n"
		<< "\t\t\tmade.push_back(*r);\n\t\t}\n"
		<< "\t\treturn made;\n\t}();\n\treturn reactions;\n}\n\n"
		<< "std::span<const ReactionNote> builtin_reaction_notes() {\n"
		<< "\tstatic constexpr ReactionNote NOTES[] = {\n";
	for (const ReactionRow &row : reactions) {
		cpp << "\t\t{ " << quoted(row.id) << ", " << row.notice_level << ", " << quoted(row.notice_says) << ", " << quoted(row.explain) << " },\n";
	}
	cpp << "\t};\n\treturn NOTES;\n}\n\n"
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

	// ---- light: the observer, the illuminant, and what each species does to it
	const auto column_of = [&](const char *file, size_t column) {
		std::ifstream in(data / "optics" / file);
		if (!in) {
			fail((data / "optics" / file).string(), "cannot be read");
		}
		std::vector<double> values;
		std::string line;
		while (std::getline(in, line)) {
			if (line.empty() || line[0] == '#') {
				continue;
			}
			std::vector<double> cells;
			std::stringstream row(line);
			std::string cell;
			while (std::getline(row, cell, ',')) {
				cells.push_back(std::stod(cell));
			}
			if (cells.size() <= column || static_cast<size_t>(cells[0]) != 380 + values.size()) {
				fail(file, "is not a table at 1 nm from 380 nm");
			}
			values.push_back(cells[column]);
		}
		if (values.size() != 401) {
			fail(file, "does not run from 380 to 780 nm");
		}
		return values;
	};
	const auto emit = [](std::ostringstream &o, const char *name, const std::vector<double> &values) {
		o << "constexpr Spectrum " << name << " = { {";
		for (size_t i = 0; i < values.size(); ++i) {
			char number[40];
			std::snprintf(number, sizeof number, "%.17g", values[i]);
			o << (i % 8 == 0 ? "\n\t" : " ") << number << ",";
		}
		o << "\n} };\n\n";
	};
	const std::string media_file = (data / "optics" / "media.json").string();
	const json media = load(data / "optics" / "media.json", "chem-optics/1");
	std::ostringstream optics;
	optics << "// Generated by chem_datagen from data/optics. Do not edit.\n#include \"chem/optics.hpp\"\n\nnamespace chem::optics {\nnamespace {\n\n";
	emit(optics, "XBAR", column_of("cie_1931_2deg.csv", 1));
	emit(optics, "YBAR", column_of("cie_1931_2deg.csv", 2));
	emit(optics, "ZBAR", column_of("cie_1931_2deg.csv", 3));
	emit(optics, "D65", column_of("d65.csv", 1));
	optics << "constexpr Spectrum NOTHING = {};\n\n";
	std::vector<std::pair<int, std::string>> kinds; // species index -> its Kind
	for (const auto &[name, m] : media.at("media").items()) {
		int index = -1;
		for (size_t i = 0; i < species.size(); ++i) {
			if (species[i].id == name) {
				index = static_cast<int>(i);
			}
		}
		if (index < 0) {
			fail(media_file, "\"" + name + "\" is not a species");
		}
		const std::string kind = m.at("kind").get<std::string>();
		// A measured table in data/optics (nm, then its values). An absorber's: coefficients per metre, summed,
		// over the concentration it has as a pure liquid (its mol_per_litre), as a molar absorptivity (decadic,
		// L/mol/cm); or, with "units": "L/mol/cm", the molar absorptivity itself. A reflector's: its diffuse
		// reflectance, a fraction (Stage 23).
		std::vector<std::pair<double, double>> measured;
		if (m.contains("table")) {
			const std::string file = m.at("table").get<std::string>();
			std::ifstream in(data / "optics" / file);
			const std::string units = m.value("units", std::string(kind == "reflect" ? "fraction" : "per_metre"));
			const bool absorptivity = kind == "absorb" && units == "L/mol/cm", reflectance = kind == "reflect" && units == "fraction";
			if (!in || !(absorptivity || reflectance || (kind == "absorb" && units == "per_metre" && m.contains("mol_per_litre")))) {
				fail(media_file, "\"" + name + "\": a table is named in data/optics: an absorber's per metre with its mol_per_litre, or in L/mol/cm; a reflector's as a fraction");
			}
			std::string line;
			while (std::getline(in, line)) {
				if (line.empty() || line[0] == '#' || line[0] == 'n') {
					continue;
				}
				std::vector<double> cells;
				std::stringstream row(line);
				std::string cell;
				while (std::getline(row, cell, ',')) {
					cells.push_back(std::stod(cell));
				}
				double sum = 0.0;
				for (size_t c = 1; c < cells.size(); ++c) {
					sum += cells[c];
				}
				if (cells.size() < 2 || (!measured.empty() && cells[0] <= measured.back().first)) {
					fail(file, "is not a table of ascending wavelengths");
				}
				measured.emplace_back(cells[0], absorptivity || reflectance ? sum : sum / 100.0 / std::log(10.0) / m.at("mol_per_litre").get<double>());
			}
			if (measured.empty() || measured.front().first > 380.0 || measured.back().first < 780.0) {
				fail(file, "does not run from 380 to 780 nm");
			}
		}
		std::vector<double> values(401, 0.0);
		for (size_t i = 0; i < values.size(); ++i) {
			const double nm = 380.0 + static_cast<double>(i);
			double v = m.value("base", 0.0);
			for (size_t k = 0; k + 1 < measured.size(); ++k) {
				if (nm >= measured[k].first && nm <= measured[k + 1].first) {
					v += measured[k].second + (measured[k + 1].second - measured[k].second) * (nm - measured[k].first) / (measured[k + 1].first - measured[k].first);
					break;
				}
			}
			if (kind == "absorb") {
				const double wavenumber = 1e7 / nm;
				for (const json &b : m.value("bands", json::array())) {
					const double d = (wavenumber - b.at("centre_cm1").get<double>()) / b.at("fwhm_cm1").get<double>();
					v += b.at("peak").get<double>() * std::exp(-4.0 * std::log(2.0) * d * d);
				}
			} else if (kind == "reflect") {
				for (const json &b : m.value("bands", json::array())) {
					const double d = (nm - b.at("centre_nm").get<double>()) / b.at("width_nm").get<double>();
					v += b.at("peak").get<double>() * std::exp(-d * d);
				}
			} else if (kind == "transmit") {
				v += m.at("rise").get<double>() / (1.0 + std::exp(-(nm - m.at("edge_nm").get<double>()) / m.at("edge_width_nm").get<double>()));
			} else {
				fail(media_file, "\"" + name + "\": a kind is absorb, reflect or transmit");
			}
			if (v < 0.0 || (kind != "absorb" && v > 1.0)) {
				fail(media_file, "\"" + name + "\": a reflectance or a transmittance is between 0 and 1");
			}
			values[i] = v;
		}
		emit(optics, ("MEDIUM_" + std::to_string(index)).c_str(), values);
		// narrow lines are kept as lines: read where each point falls, never averaged over a nanometre
		std::vector<std::array<double, 3>> line_list;
		for (const json &l : m.value("lines", json::array())) {
			if (kind != "absorb" || !(l.at("fwhm_nm").get<double>() > 0.0) || !(l.at("peak").get<double>() >= 0.0)) {
				fail(media_file, "\"" + name + "\": a line is an absorber's, with a width and a peak at least 0");
			}
			line_list.push_back({ l.at("centre_nm").get<double>(), l.at("fwhm_nm").get<double>(), l.at("peak").get<double>() });
		}
		optics << "constexpr std::array<Line, " << line_list.size() << "> LINES_" << index << (line_list.empty() ? "{};\n\n" : " = { {");
		for (const auto &l : line_list) {
			char text[120];
			std::snprintf(text, sizeof text, "\n\t{ %.17g, %.17g, %.17g },", l[0], l[1], l[2]);
			optics << text;
		}
		optics << (line_list.empty() ? "" : " } };\n\n");
		kinds.emplace_back(index, kind == "absorb" ? "Kind::Absorb" : kind == "reflect" ? "Kind::Reflect" : "Kind::Transmit");
	}
	// a refractive index at 1 nm from a Sellmeier formula (micrometres) or a table fitted by Cauchy's
	const auto index_of = [&](const std::string &name, const json &m) {
		std::vector<double> n(401, 0.0);
		if (m.contains("sellmeier_um")) {
			for (size_t i = 0; i < n.size(); ++i) {
				const double um = (380.0 + static_cast<double>(i)) / 1000.0, l2 = um * um;
				double sum = 1.0;
				for (const json &term : m.at("sellmeier_um")) {
					sum += term.at(0).get<double>() * l2 / (l2 - term.at(1).get<double>());
				}
				n[i] = std::sqrt(sum);
			}
		} else if (m.contains("table")) {
			// least squares for n = A + B / l^2 + C / l^4 (l in micrometres): the normal equations, 3 x 3
			double a[3][4] = {};
			for (const json &row : m.at("table")) {
				const double um = row.at(0).get<double>() / 1000.0, x[3] = { 1.0, 1.0 / (um * um), 1.0 / (um * um * um * um) }, y = row.at(1).get<double>();
				for (int r = 0; r < 3; ++r) {
					for (int c = 0; c < 3; ++c) {
						a[r][c] += x[r] * x[c];
					}
					a[r][3] += x[r] * y;
				}
			}
			for (int k = 0; k < 3; ++k) {
				for (int r = k + 1; r < 3; ++r) {
					const double f = a[r][k] / a[k][k];
					for (int c = k; c < 4; ++c) {
						a[r][c] -= f * a[k][c];
					}
				}
			}
			double coef[3];
			for (int k = 2; k >= 0; --k) {
				double v = a[k][3];
				for (int c = k + 1; c < 3; ++c) {
					v -= a[k][c] * coef[c];
				}
				coef[k] = v / a[k][k];
			}
			const auto cauchy = [&](double nm) { const double um = nm / 1000.0; return coef[0] + coef[1] / (um * um) + coef[2] / (um * um * um * um); };
			for (const json &row : m.at("table")) {
				if (std::fabs(cauchy(row.at(0).get<double>()) - row.at(1).get<double>()) > 2e-4) {
					fail(media_file, "\"" + name + "\": the Cauchy fit misses its table by more than 2e-4");
				}
			}
			for (size_t i = 0; i < n.size(); ++i) {
				n[i] = cauchy(380.0 + static_cast<double>(i));
			}
		} else {
			fail(media_file, "\"" + name + "\": an index is a sellmeier_um formula or a table");
		}
		for (const double v : n) {
			if (!(v >= 1.0 && v < 3.0)) {
				fail(media_file, "\"" + name + "\": an index outside [1, 3)");
			}
		}
		return n;
	};
	// refractive indices at 1 nm, and the density of what has one (for a wall's thickness)
	std::vector<std::tuple<int, std::vector<double>, double>> indices;
	const json index_entries = media.value("indices", json::object()); // held: items() of a temporary would dangle
	for (const auto &[name, m] : index_entries.items()) {
		int index = -1;
		for (size_t i = 0; i < species.size(); ++i) {
			if (species[i].id == name) {
				index = static_cast<int>(i);
			}
		}
		if (index < 0) {
			fail(media_file, "an index for \"" + name + "\", which is not a species");
		}
		const std::vector<double> n = index_of(name, m);
		emit(optics, ("INDEX_" + std::to_string(index)).c_str(), n);
		indices.emplace_back(index, n, m.value("density_g_cm3", 0.0));
	}
	optics << "constexpr Spectrum ONES = [] {\n\tSpectrum s{};\n\tfor (double &v : s) {\n\t\tv = 1.0;\n\t}\n\treturn s;\n}();\n\n";
	optics << "} // namespace\n\n"
			<< "const Spectrum &refractive_index(SpeciesId species) {\n\tswitch (species.index) {\n";
	for (const auto &[index, n, density] : indices) {
		optics << "\t\tcase " << index << ":\n\t\t\treturn INDEX_" << index << ";\n";
	}
	optics << "\t\tdefault:\n\t\t\treturn ONES;\n\t}\n}\n\ndouble index_density(SpeciesId species) {\n\tswitch (species.index) {\n";
	for (const auto &[index, n, density] : indices) {
		char text[80];
		std::snprintf(text, sizeof text, "\t\tcase %d:\n\t\t\treturn %.17g;\n", index, density);
		optics << text;
	}
	optics << "\t\tdefault:\n\t\t\treturn 0.0;\n\t}\n}\n\n";
	// optical materials that are no species: by name
	const json material_entries = media.value("materials", json::object());
	int material_count = 0;
	std::string material_cases;
	for (const auto &[name, m] : material_entries.items()) {
		const std::vector<double> n = index_of(name, m);
		emit(optics, ("MATERIAL_" + std::to_string(material_count)).c_str(), n);
		material_cases += "\tif (name == \"" + name + "\") {\n\t\treturn &MATERIAL_" + std::to_string(material_count) + ";\n\t}\n";
		++material_count;
	}
	// the lab's things that are no substance (Stage 22): what they reflect, and what they emit, by name
	for (const char *section : { "surfaces", "emitters" }) {
		const bool emits = std::string(section) == "emitters";
		std::string cases;
		int count = 0;
		const json entries = media.value(section, json::object());
		for (const auto &[name, m] : entries.items()) {
			std::vector<double> values(401, 0.0);
			for (size_t i = 0; i < values.size(); ++i) {
				const double nm = 380.0 + static_cast<double>(i);
				double v = m.value("base", 0.0);
				for (const json &b : m.value("bands", json::array())) {
					const double d = (nm - b.at("centre_nm").get<double>()) / b.at("width_nm").get<double>();
					v += b.at("peak").get<double>() * std::exp(-d * d);
				}
				if (v < 0.0 || (!emits && v > 1.0)) {
					fail(media_file, "\"" + name + "\": a reflectance is between 0 and 1, and a light at least 0");
				}
				values[i] = v;
			}
			const std::string symbol = std::string(emits ? "EMITTER_" : "SURFACE_") + std::to_string(count);
			emit(optics, symbol.c_str(), values);
			cases += "\tif (name == \"" + name + "\") {\n\t\treturn &" + symbol + ";\n\t}\n";
			++count;
		}
		optics << "const Spectrum *" << (emits ? "emitter" : "surface") << "(std::string_view name) {\n" << cases << "\treturn nullptr;\n}\n\n";
	}
	// what scatters in a liquid and is no species (Stage 45): its albedo at 1 nm, its efficiency and its asymmetry
	{
		std::string cases;
		int count = 0;
		const json scatterers = media.value("scatterers", json::object()); // (a value: items() of a temporary dangles)
		for (const auto &[name, m] : scatterers.items()) {
			if (m.value("kind", std::string()) != "scatter" || !(m.value("efficiency", 0.0) > 0.0) || !(m.value("asymmetry", 2.0) > -1.0 && m.value("asymmetry", 2.0) < 1.0)) {
				fail(media_file, "\"" + name + "\": a scatterer is of kind scatter, with an efficiency over 0 and an asymmetry in (-1, 1)");
			}
			std::vector<double> values(401, 0.0);
			for (size_t i = 0; i < values.size(); ++i) {
				const double nm = 380.0 + static_cast<double>(i);
				double v = m.value("base", 0.0);
				for (const json &b : m.value("bands", json::array())) {
					const double d = (nm - b.at("centre_nm").get<double>()) / b.at("width_nm").get<double>();
					v += b.at("peak").get<double>() * std::exp(-d * d);
				}
				if (v < 0.0 || v > 1.0) {
					fail(media_file, "\"" + name + "\": an albedo is between 0 and 1");
				}
				values[i] = v;
			}
			const std::string symbol = "SCATTER_" + std::to_string(count);
			emit(optics, symbol.c_str(), values);
			char text[200];
			std::snprintf(text, sizeof text, "constexpr Scatterer %s_IS{ &%s, %.17g, %.17g };\n\n", symbol.c_str(), symbol.c_str(), m.at("efficiency").get<double>(),
					m.at("asymmetry").get<double>());
			optics << text;
			cases += "\tif (name == \"" + name + "\") {\n\t\treturn &" + symbol + "_IS;\n\t}\n";
			++count;
		}
		optics << "const Scatterer *scatterer(std::string_view name) {\n" << cases << "\treturn nullptr;\n}\n\n";
	}
	optics << "const Spectrum *material_index(std::string_view name) {\n" << material_cases << "\treturn nullptr;\n}\n\n"
		   << "const Spectrum &xbar() {\n\treturn XBAR;\n}\nconst Spectrum &ybar() {\n\treturn YBAR;\n}\nconst Spectrum &zbar() {\n\treturn ZBAR;\n}\n"
		   << "const Spectrum &d65() {\n\treturn D65;\n}\n\nKind kind(SpeciesId species) {\n\tswitch (species.index) {\n";
	for (const auto &[index, kind] : kinds) {
		optics << "\t\tcase " << index << ":\n\t\t\treturn " << kind << ";\n";
	}
	optics << "\t\tdefault:\n\t\t\treturn Kind::None;\n\t}\n}\n\nconst Spectrum &spectrum(SpeciesId species) {\n\tswitch (species.index) {\n";
	for (const auto &[index, kind] : kinds) {
		optics << "\t\tcase " << index << ":\n\t\t\treturn MEDIUM_" << index << ";\n";
	}
	optics << "\t\tdefault:\n\t\t\treturn NOTHING;\n\t}\n}\n\nstd::span<const Line> lines(SpeciesId species) {\n\tswitch (species.index) {\n";
	for (const auto &[index, kind] : kinds) {
		optics << "\t\tcase " << index << ":\n\t\t\treturn LINES_" << index << ";\n";
	}
	optics << "\t\tdefault:\n\t\t\treturn {};\n\t}\n}\n\n} // namespace chem::optics\n";
	std::ofstream(out / "builtin_optics.cpp") << optics.str();

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
