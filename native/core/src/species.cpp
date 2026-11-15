#include "chem/species.hpp"

#include <utility>

namespace chem {

namespace {

void set_why(std::string *why, std::string text) {
	if (why != nullptr) {
		*why = std::move(text);
	}
}

} // namespace

std::optional<SpeciesTable> SpeciesTable::make(std::vector<ElementDef> elements, std::vector<SpeciesDef> species, std::string *why) {
	if (elements.empty() || elements.size() > static_cast<size_t>(MAX_ELEMENTS)) {
		set_why(why, "a table has 1 to " + std::to_string(MAX_ELEMENTS) + " elements");
		return std::nullopt;
	}
	for (size_t i = 0; i < elements.size(); ++i) {
		if (elements[i].symbol.empty() || elements[i].mass_ug_per_mol <= 0) {
			set_why(why, "element " + std::to_string(i) + " needs a symbol and a positive mass");
			return std::nullopt;
		}
		for (size_t j = 0; j < i; ++j) {
			if (elements[j].symbol == elements[i].symbol) {
				set_why(why, "element " + std::string(elements[i].symbol) + " is listed twice");
				return std::nullopt;
			}
		}
	}
	SpeciesTable table;
	table.molar_mass_.reserve(species.size());
	for (size_t i = 0; i < species.size(); ++i) {
		const SpeciesDef &s = species[i];
		const std::string name(s.id);
		if (s.id.empty()) {
			set_why(why, "species " + std::to_string(i) + " has no id");
			return std::nullopt;
		}
		for (size_t j = 0; j < i; ++j) {
			if (species[j].id == s.id) {
				set_why(why, "species " + name + " is listed twice");
				return std::nullopt;
			}
		}
		int64_t mass = 0;
		int64_t atoms = 0;
		for (size_t e = 0; e < s.atoms.size(); ++e) {
			if (s.atoms[e] < 0) {
				set_why(why, "species " + name + " has a negative atom count");
				return std::nullopt;
			}
			if (e >= elements.size() && s.atoms[e] != 0) {
				set_why(why, "species " + name + " counts an element the table does not have");
				return std::nullopt;
			}
			if (e < elements.size()) {
				mass += static_cast<int64_t>(s.atoms[e]) * elements[e].mass_ug_per_mol;
				atoms += s.atoms[e];
			}
			if (mass >= (int64_t{ 1 } << 30)) {
				set_why(why, "species " + name + " is heavier than the mass total allows (2^30 ug/mol)");
				return std::nullopt;
			}
		}
		if (atoms == 0) {
			set_why(why, "species " + name + " has no atoms");
			return std::nullopt;
		}
		if (!(s.molar_volume >= 0.0) || !(s.molar_volume - s.molar_volume == 0.0)) {
			set_why(why, "species " + name + " has a molar volume that is negative or not finite");
			return std::nullopt;
		}
		for (const double v : { s.hf, s.cp, s.s0 }) {
			if (s.has_thermo && !(v - v == 0.0)) {
				set_why(why, "species " + name + " has thermodynamic data that is not finite");
				return std::nullopt;
			}
		}
		table.molar_mass_.push_back(mass);
	}
	table.elements_ = std::move(elements);
	table.species_ = std::move(species);
	return table;
}

SpeciesId SpeciesTable::find(std::string_view id) const {
	for (size_t i = 0; i < species_.size(); ++i) {
		if (species_[i].id == id) {
			return SpeciesId{ static_cast<int32_t>(i) };
		}
	}
	return SpeciesId{};
}

int32_t Reaction::nu(SpeciesId s) const {
	for (const Term &t : terms()) {
		if (t.species == s) {
			return t.nu;
		}
	}
	return 0;
}

std::optional<Reaction> Reaction::make(const SpeciesTable &table, std::string_view id, std::span<const Term> terms, std::string *why, Kinetics kinetics) {
	const std::string name(id);
	if (terms.empty() || terms.size() > static_cast<size_t>(MAX_TERMS)) {
		set_why(why, "reaction " + name + " has 1 to " + std::to_string(MAX_TERMS) + " terms");
		return std::nullopt;
	}
	bool reactant = false;
	bool product = false;
	for (size_t i = 0; i < terms.size(); ++i) {
		if (!table.has(terms[i].species)) {
			set_why(why, "reaction " + name + " names a species the table does not have");
			return std::nullopt;
		}
		if (terms[i].nu == 0) {
			set_why(why, "reaction " + name + " has a zero stoichiometric number");
			return std::nullopt;
		}
		if (table.species(terms[i].species).wall) {
			set_why(why, "reaction " + name + " names a wall species: a vessel's material does not react");
			return std::nullopt;
		}
		for (size_t j = 0; j < i; ++j) {
			if (terms[j].species == terms[i].species) {
				set_why(why, "reaction " + name + " names a species twice");
				return std::nullopt;
			}
		}
		reactant = reactant || terms[i].nu < 0;
		product = product || terms[i].nu > 0;
	}
	if (!reactant || !product) {
		set_why(why, "reaction " + name + " needs a reactant and a product");
		return std::nullopt;
	}
	if (!balanced(table.all_species(), terms)) {
		set_why(why, "reaction " + name + " does not balance in every element and in charge");
		return std::nullopt;
	}
	if (kinetics.law == Kinetics::Law::FirstOrder) {
		bool is_reactant = false;
		for (const Term &t : terms) {
			is_reactant = is_reactant || (t.species == kinetics.in && t.nu < 0);
		}
		if (!is_reactant) {
			set_why(why, "reaction " + name + ": the species its rate is first order in is not one of its reactants");
			return std::nullopt;
		}
		const bool finite = kinetics.prefactor - kinetics.prefactor == 0.0 && kinetics.activation - kinetics.activation == 0.0;
		if (!finite || !(kinetics.prefactor > 0.0) || kinetics.activation < 0.0) {
			set_why(why, "reaction " + name + ": a prefactor is positive, an activation energy is not negative, both are finite");
			return std::nullopt;
		}
	}
	Reaction r;
	r.table_ = &table;
	r.kinetics_ = kinetics;
	r.id_ = name;
	for (size_t i = 0; i < terms.size(); ++i) {
		r.terms_[i] = terms[i];
	}
	r.term_count_ = static_cast<int32_t>(terms.size());
	return r;
}

} // namespace chem
