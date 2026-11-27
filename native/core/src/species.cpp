#include "chem/species.hpp"

#include <algorithm>
#include <utility>

namespace chem {

namespace {

void set_why(std::string *why, std::string text) {
	if (why != nullptr) {
		*why = std::move(text);
	}
}

} // namespace

std::optional<SpeciesTable> SpeciesTable::make(std::vector<ElementDef> elements, std::vector<SpeciesDef> species, std::string *why, SolutionDef solution) {
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
		// (Stage 63) only what is dissolved carries a charge: matter moves between cells as neutral matter, and a
		// charged solid or gas would have no counter-ion to move with
		if (s.charge != 0 && s.phase != Phase::Aqueous) {
			set_why(why, "species " + name + " is charged and not dissolved");
			return std::nullopt;
		}
		// (Stage 63: an ion's apparent molar volume, by the convention of H+'s at nought, may be negative)
		if (!(s.molar_volume >= 0.0 || ((s.charge != 0 || s.retired) && s.phase == Phase::Aqueous)) || !(s.molar_volume - s.molar_volume == 0.0)) {
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
	// (Stage 64) each basis: masters that are dissolved, not retired and their own masters, summing to the species'
	// charge and to its atoms but for whole waters
	int hydrogen = -1, oxygen = -1;
	for (size_t e = 0; e < elements.size(); ++e) {
		hydrogen = elements[e].symbol == std::string_view("H") ? static_cast<int>(e) : hydrogen;
		oxygen = elements[e].symbol == std::string_view("O") ? static_cast<int>(e) : oxygen;
	}
	for (const SpeciesDef &s : species) {
		if (s.basis[0].count == 0) {
			continue;
		}
		const std::string name(s.id);
		if (s.phase != Phase::Aqueous || s.retired) {
			set_why(why, "species " + name + " has a basis, and only a dissolved species that is not retired has one");
			return std::nullopt;
		}
		std::array<int64_t, MAX_ELEMENTS> rest{};
		int64_t charge = s.charge;
		for (size_t e = 0; e < rest.size(); ++e) {
			rest[e] = s.atoms[e];
		}
		for (const BasisCount &b : s.basis) {
			if (b.count == 0) {
				break;
			}
			if (b.species < 0 || static_cast<size_t>(b.species) >= species.size()) {
				set_why(why, "species " + name + ": its basis names a species out of range");
				return std::nullopt;
			}
			const SpeciesDef &m = species[static_cast<size_t>(b.species)];
			if (m.phase != Phase::Aqueous || m.retired || m.basis[0].count != 0) {
				set_why(why, "species " + name + ": its basis names " + std::string(m.id) + ", which is not a master (dissolved, not retired, with no basis)");
				return std::nullopt;
			}
			for (size_t e = 0; e < rest.size(); ++e) {
				rest[e] -= static_cast<int64_t>(b.count) * m.atoms[e];
			}
			charge -= static_cast<int64_t>(b.count) * m.charge;
		}
		bool water = charge == 0 && oxygen >= 0 && hydrogen >= 0 && rest[static_cast<size_t>(hydrogen)] == 2 * rest[static_cast<size_t>(oxygen)];
		for (size_t e = 0; e < rest.size(); ++e) {
			water = water && (static_cast<int>(e) == hydrogen || static_cast<int>(e) == oxygen || rest[e] == 0);
		}
		if (!water && !(charge == 0 && std::all_of(rest.begin(), rest.end(), [](int64_t v) { return v == 0; }))) {
			set_why(why, "species " + name + ": its basis does not sum to its charge and its atoms but for whole waters");
			return std::nullopt;
		}
	}
	// the solution model (GAME_SPEC §4.6, §10)
	const auto finite = [](double v) { return v - v == 0.0; };
	if (!finite(solution.a_phi) || !(solution.b > 0.0) || !finite(solution.b)) {
		set_why(why, "the solution model needs a finite A_phi and a positive b");
		return std::nullopt;
	}
	for (size_t i = 0; i < solution.ions.size(); ++i) {
		if (solution.ions[i].name.empty()) {
			set_why(why, "ion " + std::to_string(i) + " has no name");
			return std::nullopt;
		}
		if (!(solution.ions[i].diffusion >= 0.0) || !finite(solution.ions[i].diffusion)) {
			set_why(why, "ion " + std::string(solution.ions[i].name) + " has a diffusion coefficient that is negative or not finite");
			return std::nullopt;
		}
		if (!(solution.ions[i].walden > 0.0 && solution.ions[i].walden <= 2.0)) { // Stage 65
			set_why(why, "ion " + std::string(solution.ions[i].name) + " has a Walden exponent outside (0, 2]");
			return std::nullopt;
		}
		for (size_t j = 0; j < i; ++j) {
			if (solution.ions[j].name == solution.ions[i].name) {
				set_why(why, "ion " + std::string(solution.ions[i].name) + " is listed twice");
				return std::nullopt;
			}
		}
	}
	for (size_t i = 0; i < solution.viscosity.size(); ++i) {
		const ViscosityPoint &v = solution.viscosity[i];
		if (!(v.kelvin > 0.0) || !finite(v.kelvin) || !(v.pascal_seconds > 0.0) || !finite(v.pascal_seconds) || (i > 0 && !(v.kelvin > solution.viscosity[i - 1].kelvin))) {
			set_why(why, "the solvent's viscosity is a table of positive values at rising temperatures");
			return std::nullopt;
		}
	}
	// (Stage 65) the solvent's dielectric constant, and the ions' size
	for (size_t i = 0; i < solution.dielectric.size(); ++i) {
		const DielectricPoint &d = solution.dielectric[i];
		if (!(d.kelvin > 0.0) || !finite(d.kelvin) || !(d.epsilon > 1.0) || !finite(d.epsilon) || (i > 0 && !(d.kelvin > solution.dielectric[i - 1].kelvin))) {
			set_why(why, "the solvent's dielectric constant is a table of values above 1 at rising temperatures");
			return std::nullopt;
		}
	}
	if (!(solution.ion_size >= 0.0 && solution.ion_size < 1e-8)) {
		set_why(why, "the ions' size is a length from 0 to 100 Angstrom");
		return std::nullopt;
	}
	// the solvent's density (Stage 67): none, or finite with a range, positive at both its ends and between
	if (std::any_of(solution.density.begin(), solution.density.end(), [](double c) { return c != 0.0; })) {
		bool good = std::all_of(solution.density.begin(), solution.density.end(), [&](double c) { return finite(c); }) && finite(solution.density_b) && finite(solution.density_low) &&
				finite(solution.density_high) && solution.density_low < solution.density_high;
		for (int i = 0; good && i <= 100; ++i) {
			const double t = solution.density_low + (solution.density_high - solution.density_low) * i / 100.0;
			double n = 0.0;
			for (size_t k = solution.density.size(); k-- > 0;) {
				n = n * t + solution.density[k];
			}
			good = 1.0 + solution.density_b * t > 0.0 && n > 0.0;
		}
		if (!good) {
			set_why(why, "the solvent's density is finite and positive over its range, a range from low to high");
			return std::nullopt;
		}
	}
	const auto ion_count = static_cast<int32_t>(solution.ions.size());
	for (SpeciesDef &s : species) {
		const std::string name(s.id);
		int64_t charge = 0;
		size_t named = 0;
		for (const IonCount &ion : s.ions) {
			if (ion.count == 0 && ion.ion < 0) {
				break;
			}
			if (ion.ion < 0 || ion.ion >= ion_count || ion.count <= 0) {
				set_why(why, "species " + name + " names an ion out of range, or a count that is not positive");
				return std::nullopt;
			}
			charge += static_cast<int64_t>(ion.count) * solution.ions[static_cast<size_t>(ion.ion)].charge;
			++named;
		}
		if (named > 0 && s.phase != Phase::Aqueous) {
			set_why(why, "species " + name + " names ions, and only a dissolved species has them");
			return std::nullopt;
		}
		if (named > 0 && charge != s.charge) {
			set_why(why, "species " + name + ": the charges of its ions do not sum to its own");
			return std::nullopt;
		}
		if (named == 0 && s.phase == Phase::Aqueous) {
			// a neutral particle of its own
			solution.ions.push_back(IonDef{ s.id, s.charge });
			s.ions = {};
			s.ions[0] = IonCount{ static_cast<int32_t>(solution.ions.size() - 1), 1 };
		}
	}
	if (solution.ions.size() > static_cast<size_t>(MAX_PARTICLES)) {
		set_why(why, "a table has at most " + std::to_string(MAX_PARTICLES) + " particles in solution");
		return std::nullopt;
	}
	for (const PairDef &p : solution.pairs) {
		const bool cation = p.cation >= 0 && p.cation < ion_count && solution.ions[static_cast<size_t>(p.cation)].charge > 0;
		const bool anion = p.anion >= 0 && p.anion < ion_count && solution.ions[static_cast<size_t>(p.anion)].charge < 0;
		if (!cation || !anion) {
			set_why(why, "a pair of the solution model is not one cation and one anion");
			return std::nullopt;
		}
		if (!finite(p.beta0) || !finite(p.beta1) || !finite(p.beta2) || !finite(p.c_phi) || !(p.alpha1 > 0.0) || !(p.alpha2 > 0.0) || !finite(p.alpha1) ||
				!finite(p.alpha2)) {
			set_why(why, "a pair of the solution model has a parameter that is not finite, or an alpha that is not positive");
			return std::nullopt;
		}
	}
	table.elements_ = std::move(elements);
	table.species_ = std::move(species);
	table.solution_ = std::move(solution);
	return table;
}

std::vector<std::pair<SpeciesId, int32_t>> SpeciesTable::as_species(SpeciesId s) const {
	std::vector<std::pair<SpeciesId, int32_t>> out;
	if (!has(s)) {
		return out;
	}
	const SpeciesDef &def = species(s);
	if (!def.retired) {
		out.emplace_back(s, 1);
		return out;
	}
	for (const IonCount &ion : def.ions) {
		if (ion.count <= 0) {
			break;
		}
		for (int k = 0; k < species_count(); ++k) {
			const SpeciesDef &d = species(SpeciesId{ k });
			if (!d.retired && d.phase == Phase::Aqueous && d.charge != 0 && d.ions[0].ion == ion.ion && d.ions[0].count == 1 && d.ions[1].count <= 0) {
				out.emplace_back(SpeciesId{ k }, ion.count);
				break;
			}
		}
	}
	return out;
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
	if (kinetics.law == Kinetics::Law::Surface) {
		bool names_solid = false;
		bool driver_ok = !table.has(kinetics.driver);
		for (const Term &t : terms) {
			const SpeciesDef &def = table.species(t.species);
			names_solid = names_solid || (t.species == kinetics.in && def.phase == Phase::Solid && def.molar_volume > 0.0);
			driver_ok = driver_ok || (t.species == kinetics.driver && t.nu < 0 && def.phase == Phase::Aqueous);
		}
		if (!names_solid) {
			set_why(why, "reaction " + name + ": the solid at whose surface it happens is not one of its solids with a molar volume");
			return std::nullopt;
		}
		if (!driver_ok) {
			set_why(why, "reaction " + name + ": the driver of its rate is not one of its dissolved reactants");
			return std::nullopt;
		}
		for (const double v : { kinetics.prefactor, kinetics.activation, kinetics.nucleation, kinetics.nucleation_order, kinetics.secondary, kinetics.secondary_order, kinetics.nucleation_barrier }) {
			if (!(v >= 0.0) || !(v - v == 0.0)) {
				set_why(why, "reaction " + name + ": a rate's numbers are not negative and are finite");
				return std::nullopt;
			}
		}
		if (!(kinetics.prefactor > 0.0)) {
			set_why(why, "reaction " + name + ": a prefactor is positive");
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

double formula_units(const SpeciesTable &table, std::span<const std::pair<SpeciesId, double>> dissolved, SpeciesId unit) {
	if (!table.has(unit) || !table.species(unit).retired) {
		return 0.0;
	}
	// a species as its masters: its basis, or itself; a retired unit, its ions' (Stage 64)
	const auto basis_of = [&](SpeciesId s, auto &&add) {
		const SpeciesDef &d = table.species(s);
		if (d.basis[0].count == 0) {
			add(s.index, 1.0);
			return;
		}
		for (const BasisCount &b : d.basis) {
			if (b.count == 0) {
				break;
			}
			add(b.species, static_cast<double>(b.count));
		}
	};
	// what the matter holds of each master
	std::vector<double> held(static_cast<size_t>(table.species_count()), 0.0);
	for (const auto &[s, mol] : dissolved) {
		if (!table.has(s) || table.species(s).phase != Phase::Aqueous) {
			continue;
		}
		for (const auto &[part, count] : table.as_species(s)) {
			basis_of(part, [&, m = mol](int32_t master, double n) { held[static_cast<size_t>(master)] += m * static_cast<double>(count) * n; });
		}
	}
	// the unit's own masters, and the fewest units they make
	std::vector<double> needs(static_cast<size_t>(table.species_count()), 0.0);
	for (const auto &[part, count] : table.as_species(unit)) {
		basis_of(part, [&](int32_t master, double n) { needs[static_cast<size_t>(master)] += static_cast<double>(count) * n; });
	}
	double fewest = 0.0;
	bool any = false;
	for (size_t m = 0; m < needs.size(); ++m) {
		if (needs[m] == 0.0) {
			continue;
		}
		const double whole = held[m] / needs[m]; // a master given up (OH-'s hydrogen ion): the missing over the given
		fewest = !any || whole < fewest ? whole : fewest;
		any = true;
	}
	return fewest > 0.0 ? fewest : 0.0;
}

} // namespace chem
