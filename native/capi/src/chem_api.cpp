#include "chem_api.h"

#include "chem/error.hpp"
#include "chem/species.hpp"
#include "lab/lab.hpp"

#include <string>
#include <vector>

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

} // extern "C"
