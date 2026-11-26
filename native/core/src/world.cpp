#include "chem/world.hpp"

#include <algorithm>

#include "chem/invariants.hpp"
#include "chem/thermo.hpp"

#include <utility>

namespace chem {

const char *refusal_text(Refusal r) {
	switch (r) {
		case Refusal::None:
			return "done";
		case Refusal::UnknownCell:
			return "no such cell";
		case Refusal::UnknownEdge:
			return "no such open edge";
		case Refusal::NotOnEdge:
			return "the giving cell is not an end of the edge";
		case Refusal::UnknownSpecies:
			return "no such species";
		case Refusal::PhaseNotAllowed:
			return "the edge does not let this phase through";
		case Refusal::NegativeAmount:
			return "a negative amount";
		case Refusal::Insufficient:
			return "the cell does not hold that much";
		case Refusal::Overflow:
			return "the result does not fit the integer range";
		case Refusal::ForeignReaction:
			return "a reaction of another species table";
		case Refusal::MidStep:
			return "the topology changes at a step boundary only";
		case Refusal::SameCell:
			return "an edge joins two different cells";
		case Refusal::Reservoir:
			return "a reservoir holds energy, never matter";
		case Refusal::BadValue:
			return "the value must be finite and not negative";
		case Refusal::Wall:
			return "a vessel's own material does not move";
		case Refusal::WouldSeal:
			return "a rigid cell would be sealed while it is not full at the room's pressure";
	}
	return "unknown refusal";
}

// ---------------------------------------------------------------- the builder

World::Builder::Builder(const SpeciesTable &table) :
		table_(&table) {}

namespace {

bool good_conductance(double c) {
	return c >= 0.0 && c - c == 0.0;
}

} // namespace

std::vector<CellId> sealed_cells(std::span<const CellMeta> meta, std::span<const Edge> edges) {
	// flood from the elastic cells along the gas edges, each cell's gas edges listed once (Stage 68: a flood that
	// scanned every edge at every cell it reached grew as cells times edges; what it reaches is the same)
	const size_t n = meta.size();
	std::vector<size_t> first(n + 1, 0), to;
	const auto gas_path = [&](const Edge &e) {
		// (a column's edge is no gas path: Stage 47)
		return e.open && (e.phases & PHASES_GAS) != 0 && !e.column && static_cast<size_t>(e.a.index) < n && static_cast<size_t>(e.b.index) < n &&
				e.a.index >= 0 && e.b.index >= 0;
	};
	for (const Edge &e : edges) {
		if (gas_path(e)) {
			++first[static_cast<size_t>(e.a.index) + 1];
			++first[static_cast<size_t>(e.b.index) + 1];
		}
	}
	for (size_t c = 0; c < n; ++c) {
		first[c + 1] += first[c];
	}
	to.resize(first[n]);
	std::vector<size_t> fill(first.begin(), first.end() - 1);
	for (const Edge &e : edges) {
		if (gas_path(e)) {
			const auto a = static_cast<size_t>(e.a.index), b = static_cast<size_t>(e.b.index);
			to[fill[a]++] = b;
			to[fill[b]++] = a;
		}
	}
	std::vector<uint8_t> reached(n, 0);
	std::vector<size_t> frontier;
	for (size_t c = 0; c < n; ++c) {
		if (!meta[c].reservoir && !meta[c].rigid && !meta[c].layer) { // (a layer is no root of the air: Stage 47)
			reached[c] = 1;
			frontier.push_back(c);
		}
	}
	while (!frontier.empty()) {
		const size_t at = frontier.back();
		frontier.pop_back();
		for (size_t k = first[at]; k < first[at + 1]; ++k) {
			const size_t other = to[k];
			if (!reached[other] && !meta[other].reservoir) {
				reached[other] = 1;
				frontier.push_back(other);
			}
		}
	}
	std::vector<CellId> sealed;
	for (size_t c = 0; c < meta.size(); ++c) {
		if (meta[c].rigid && !reached[c]) {
			sealed.push_back(CellId{ static_cast<int32_t>(c) });
		}
	}
	return sealed;
}

CellId World::Builder::add_cell(std::string name) {
	names_.push_back(std::move(name));
	meta_.push_back(CellMeta{});
	return CellId{ static_cast<int32_t>(names_.size() - 1) };
}

CellId World::Builder::add_rigid_cell(std::string name, double volume) {
	names_.push_back(std::move(name));
	CellMeta m;
	m.rigid = true;
	m.volume = volume;
	meta_.push_back(std::move(m));
	return CellId{ static_cast<int32_t>(names_.size() - 1) };
}

CellId World::Builder::add_reservoir(std::string name, double temperature) {
	names_.push_back(std::move(name));
	CellMeta m;
	m.reservoir = true;
	m.reservoir_temperature = temperature;
	meta_.push_back(std::move(m));
	return CellId{ static_cast<int32_t>(names_.size() - 1) };
}

EdgeId World::Builder::add_edge(CellId a, CellId b, PhaseMask phases, double conductance) {
	edges_.push_back(Edge{ a, b, phases, true, conductance });
	return EdgeId{ static_cast<int32_t>(edges_.size() - 1) };
}

CellId World::Builder::add_layer_cell(std::string name) {
	names_.push_back(std::move(name));
	CellMeta m;
	m.layer = true;
	meta_.push_back(m);
	return CellId{ static_cast<int32_t>(names_.size() - 1) };
}

EdgeId World::Builder::add_column_edge(CellId lower, CellId upper) {
	Edge e{ lower, upper, PHASES_ALL, true, 0.0 };
	e.column = true;
	edges_.push_back(e);
	return EdgeId{ static_cast<int32_t>(edges_.size() - 1) };
}

World::Builder &World::Builder::set_column(CellId top, double area, double whole) {
	if (top.index >= 0 && static_cast<size_t>(top.index) < meta_.size()) {
		CellMeta &m = meta_[static_cast<size_t>(top.index)];
		m.column_volume = whole > 0.0 ? whole : m.volume;
		m.column_area = area;
	}
	return *this;
}

World::Builder &World::Builder::set_column_shape(CellId top, double rise, std::vector<double> volumes) {
	if (top.index >= 0 && static_cast<size_t>(top.index) < meta_.size() && rise > 0.0 && volumes.size() >= 2) {
		CellMeta &m = meta_[static_cast<size_t>(top.index)];
		m.column_rise = rise;
		m.column_shape = std::move(volumes);
	}
	return *this;
}

EdgeId World::Builder::add_drain(CellId from, CellId to) {
	edges_.push_back(Edge{ from, to, PHASES_LIQUID, true, 0.0, true });
	return EdgeId{ static_cast<int32_t>(edges_.size() - 1) };
}

EdgeId World::Builder::add_edge_as(const Edge &edge) {
	edges_.push_back(edge);
	return EdgeId{ static_cast<int32_t>(edges_.size() - 1) };
}

World::Builder &World::Builder::set_spill(CellId cell) {
	spill_ = cell;
	return *this;
}

World::Builder &World::Builder::set_mixing(CellId cell, double mixing) {
	if (cell.index >= 0 && static_cast<size_t>(cell.index) < meta_.size()) {
		meta_[static_cast<size_t>(cell.index)].mixing = mixing;
	}
	return *this;
}

World::Builder &World::Builder::set_boundary(CellId cell, bool frozen, bool retired) {
	if (cell.index >= 0 && static_cast<size_t>(cell.index) < meta_.size()) {
		meta_[static_cast<size_t>(cell.index)].frozen = frozen || retired;
		meta_[static_cast<size_t>(cell.index)].retired = retired;
	}
	return *this;
}

World::Builder &World::Builder::set_step(uint64_t step) {
	step_ = step;
	return *this;
}

World::Builder &World::Builder::set_temperature(CellId cell, double temperature) {
	temperatures_.push_back(SetTemperature{ cell, temperature });
	return *this;
}

World::Builder &World::Builder::set_amount(CellId cell, SpeciesId species, Amount amount) {
	// a retired formula unit is its ions (Stage 63): its amount adds to theirs
	if (table_->has(species) && table_->species(species).retired) {
		for (const auto &[ion, count] : table_->as_species(species)) {
			amounts_.push_back(Set{ cell, ion, Amount{ amount.quanta * count }, true });
		}
		return *this;
	}
	amounts_.push_back(Set{ cell, species, amount });
	return *this;
}

World::Builder &World::Builder::set_particles(CellId cell, SpeciesId species, int64_t count) {
	particles_.push_back(Set{ cell, species, Amount{ count } });
	return *this;
}

World::Builder &World::Builder::set_energy(CellId cell, Energy energy) {
	energies_.push_back(SetEnergy{ cell, energy });
	return *this;
}

std::optional<World> World::Builder::build(std::string *why) const {
	const auto refuse = [why](const char *text) {
		if (why != nullptr) {
			*why = text;
		}
		return std::nullopt;
	};
	const auto cells = static_cast<int32_t>(names_.size());
	const auto known_cell = [cells](CellId c) { return c.index >= 0 && c.index < cells; };

	World world;
	world.table_ = table_;
	world.names_ = names_;
	world.meta_ = meta_;
	const auto species_count = static_cast<size_t>(table_->species_count());
	world.cells_.assign(names_.size(), CellState{ std::vector<int64_t>(species_count, 0), 0, std::vector<int64_t>(species_count, 0) });
	for (const Edge &e : edges_) {
		if (!known_cell(e.a) || !known_cell(e.b)) {
			return refuse("an edge names a cell the world does not have");
		}
		if (e.a == e.b) {
			return refuse("an edge joins two different cells");
		}
		if (!good_conductance(e.conductance)) {
			return refuse("a conductance is negative or not finite");
		}
	}
	world.edges_ = edges_;
	world.refresh_sealed();
	for (const Set &s : amounts_) {
		if (!known_cell(s.cell)) {
			return refuse("an amount is set in a cell the world does not have");
		}
		if (!table_->has(s.species)) {
			return refuse("an amount is set for a species the table does not have");
		}
		if (s.amount.quanta < 0) {
			return refuse("a negative amount");
		}
		if (meta_[static_cast<size_t>(s.cell.index)].reservoir && s.amount.quanta != 0) {
			return refuse("a reservoir holds energy, never matter");
		}
		int64_t &held = world.cells_[static_cast<size_t>(s.cell.index)].amount[static_cast<size_t>(s.species.index)];
		if (s.add && add_overflows(held, s.amount.quanta)) {
			return refuse("totals that do not fit the integer range");
		}
		held = s.add ? held + s.amount.quanta : s.amount.quanta;
	}
	// (Stage 63) every cell neutral: matter moves only as neutral matter, so what is built charged would stay so
	for (const CellState &cell : world.cells_) {
		int64_t charge = 0;
		for (size_t s = 0; s < cell.amount.size(); ++s) {
			charge += static_cast<int64_t>(table_->species(SpeciesId{ static_cast<int32_t>(s) }).charge) * cell.amount[s];
		}
		if (charge != 0) {
			return refuse("a cell's charge is not nought");
		}
	}
	for (const SetEnergy &s : energies_) {
		if (!known_cell(s.cell)) {
			return refuse("an energy is set in a cell the world does not have");
		}
		world.cells_[static_cast<size_t>(s.cell.index)].energy = s.energy.quanta;
	}
	// particles: as set, else one lump of each solid that is there
	for (CellState &cell : world.cells_) {
		for (size_t s = 0; s < species_count; ++s) {
			const SpeciesDef &def = table_->species(SpeciesId{ static_cast<int32_t>(s) });
			cell.particles[s] = cell.amount[s] > 0 && def.phase == Phase::Solid && !def.wall ? 1 : 0;
		}
	}
	for (const Set &s : particles_) {
		if (!known_cell(s.cell) || !table_->has(s.species)) {
			return refuse("a particle count is set for a cell or a species the world does not have");
		}
		const SpeciesDef &def = table_->species(s.species);
		CellState &cell = world.cells_[static_cast<size_t>(s.cell.index)];
		const bool there = cell.amount[static_cast<size_t>(s.species.index)] > 0;
		if (def.phase != Phase::Solid || def.wall || (there ? s.amount.quanta < 1 : s.amount.quanta != 0)) {
			return refuse("a particle count is for a solid, at least one where the solid is, none where it is not");
		}
		cell.particles[static_cast<size_t>(s.species.index)] = s.amount.quanta;
	}
	for (const CellMeta &m : meta_) {
		if (m.reservoir) {
			world.thermal_ = true;
			if (!(m.reservoir_temperature > 0.0) || !(m.reservoir_temperature - m.reservoir_temperature == 0.0)) {
				return refuse("a reservoir's temperature is positive and finite");
			}
		}
	}
	for (const SetTemperature &s : temperatures_) {
		if (!known_cell(s.cell)) {
			return refuse("a temperature is set in a cell the world does not have");
		}
		if (meta_[static_cast<size_t>(s.cell.index)].reservoir) {
			return refuse("a reservoir's temperature is fixed when it is added");
		}
		if (!(s.temperature > 0.0) || !(s.temperature - s.temperature == 0.0)) {
			return refuse("a temperature is positive and finite");
		}
		CellState &cell = world.cells_[static_cast<size_t>(s.cell.index)];
		const CellMeta &m = world.meta_[static_cast<size_t>(s.cell.index)];
		if (!thermal_state(*table_, cell, &m).defined) {
			return refuse("a temperature is set in a cell that cannot have one (a species without thermodynamic data, or no heat capacity, or sealed with no room for its gas)");
		}
		bool ok = false;
		double joule = enthalpy_at(*table_, cell, s.temperature, ok);
		if (m.sealed) { // E = H(T) - n_gas R T + P_atm V_free (chem/thermo.hpp)
			double gas = 0.0;
			for (size_t k = 0; k < cell.amount.size(); ++k) {
				gas += table_->species(SpeciesId{ static_cast<int32_t>(k) }).phase == Phase::Gas ? static_cast<double>(cell.amount[k]) * MOL_PER_QUANTUM : 0.0;
			}
			joule += -gas * GAS_CONSTANT * s.temperature + P_ATMOSPHERE * (m.volume - fill_of(*table_, cell, T0).condensed);
		}
		const Energy energy = energy_from_joule(joule, ok);
		if (!ok) {
			return refuse("a cell's enthalpy does not fit the integer range");
		}
		cell.energy = energy.quanta;
		world.thermal_ = true;
	}
	if (world.thermal_) {
		for (size_t c = 0; c < world.cells_.size(); ++c) {
			const CellState &cell = world.cells_[c];
			const bool none = std::all_of(cell.amount.begin(), cell.amount.end(), [](int64_t a) { return a == 0; });
			if (none && cell.energy == 0) {
				continue; // an empty cell (Stage 47: a column's layer while it is mixed) has no temperature, and holds no energy
			}
			if (!meta_[c].reservoir && !thermal_state(*table_, cell, &world.meta_[c]).defined) {
				return refuse("in a world with temperatures every cell has one (a species without thermodynamic data, or no heat capacity)");
			}
		}
	}
	if (world.thermal_) {
		for (const CellState &cell : world.cells_) {
			if (dry_solute(*table_, cell)) {
				return refuse("a cell holds a dissolved species and no liquid to hold it");
			}
		}
	}
	for (size_t c = 0; c < world.cells_.size(); ++c) {
		if (!meta_[c].rigid) {
			continue;
		}
		if (!(meta_[c].volume > 0.0) || !(meta_[c].volume - meta_[c].volume == 0.0)) {
			return refuse("a rigid cell's volume is positive and finite");
		}
		const Fill fill = fill_of(*table_, world.cells_[c], T0);
		if (!fill.defined) {
			return refuse("a rigid cell holds a condensed species whose molar volume is unknown");
		}
		if (fill.condensed > meta_[c].volume) {
			return refuse("a rigid cell's solids and liquids do not fit in it");
		}
	}
	if (spill_.index >= 0) {
		if (!known_cell(spill_) || meta_[static_cast<size_t>(spill_.index)].rigid || meta_[static_cast<size_t>(spill_.index)].reservoir) {
			return refuse("what overflows goes into an elastic cell");
		}
		world.spill_ = spill_;
	}
	for (const CellMeta &m : meta_) {
		if (!good_conductance(m.mixing)) {
			return refuse("a cell's mixing is negative or not finite");
		}
	}
	world.step_ = step_;
	// The totals fit the integer range now, so they fit it forever: the
	// primitives never change them.
	if (!totals_fit(*table_, Snapshot{ world.cells_ })) {
		return refuse("the world's totals do not fit the integer range");
	}
	world.before_ = Snapshot{ world.cells_ };
	return world;
}

// ------------------------------------------------------------------ the world

Amount World::amount(CellId cell, SpeciesId species) const {
	if (!known(cell) || !table_->has(species)) {
		return Amount{};
	}
	return Amount{ cells_[static_cast<size_t>(cell.index)].amount[static_cast<size_t>(species.index)] };
}

int64_t World::particles(CellId cell, SpeciesId species) const {
	if (!known(cell) || !table_->has(species)) {
		return 0;
	}
	return cells_[static_cast<size_t>(cell.index)].particles[static_cast<size_t>(species.index)];
}

Refusal World::set_particles(CellId cell, SpeciesId species, int64_t count) {
	if (!known(cell)) {
		return Refusal::UnknownCell;
	}
	if (!table_->has(species)) {
		return Refusal::UnknownSpecies;
	}
	const SpeciesDef &def = table_->species(species);
	const bool there = cells_[static_cast<size_t>(cell.index)].amount[static_cast<size_t>(species.index)] > 0;
	if (def.phase != Phase::Solid || def.wall || (there ? count < 1 : count != 0)) {
		return Refusal::BadValue;
	}
	cells_[static_cast<size_t>(cell.index)].particles[static_cast<size_t>(species.index)] = count;
	return Refusal::None;
}

Refusal World::resize_column_top(CellId top, double volume) {
	if (!known(top)) {
		return Refusal::UnknownCell;
	}
	CellMeta &m = meta_[static_cast<size_t>(top.index)];
	if (!m.rigid || !(m.column_volume > 0.0) || !(volume > 0.0) || volume > m.column_volume) {
		return Refusal::BadValue;
	}
	m.volume = volume;
	return Refusal::None;
}

Refusal World::set_stirring(CellId cell, double watts) {
	if (!known(cell)) {
		return Refusal::UnknownCell;
	}
	if (!good_conductance(watts)) { // (at least 0 and finite)
		return Refusal::BadValue;
	}
	meta_[static_cast<size_t>(cell.index)].stirring = watts;
	return Refusal::None;
}

Refusal World::set_landing(CellId cell, double speed) {
	if (!known(cell)) {
		return Refusal::UnknownCell;
	}
	if (!good_conductance(speed)) { // (at least 0 and finite)
		return Refusal::BadValue;
	}
	meta_[static_cast<size_t>(cell.index)].landing = speed;
	return Refusal::None;
}

Refusal World::set_mixing(CellId cell, double mixing) {
	if (!ledger_.empty()) {
		return Refusal::MidStep;
	}
	if (!known(cell)) {
		return Refusal::UnknownCell;
	}
	if (!good_conductance(mixing)) {
		return Refusal::BadValue;
	}
	meta_[static_cast<size_t>(cell.index)].mixing = mixing;
	return Refusal::None;
}

Energy World::energy(CellId cell) const {
	if (!known(cell)) {
		return Energy{};
	}
	return Energy{ cells_[static_cast<size_t>(cell.index)].energy };
}

bool World::temperature(CellId cell, double &kelvin) const {
	if (!known(cell)) {
		return false;
	}
	const CellMeta &m = meta_[static_cast<size_t>(cell.index)];
	if (m.reservoir) {
		kelvin = m.reservoir_temperature;
		return true;
	}
	const Thermal t = thermal_state(*table_, cells_[static_cast<size_t>(cell.index)], &m);
	kelvin = t.temperature;
	return t.defined;
}

// Particle counts follow the solids (GAME_SPEC §9: X1): a solid that is gone
// has no particles; one that appears is one lump until told otherwise.
void World::tend_particles(CellState &cell, SpeciesId species) {
	const SpeciesDef &def = table_->species(species);
	if (def.phase != Phase::Solid || def.wall) {
		return;
	}
	const auto s = static_cast<size_t>(species.index);
	if (cell.amount[s] == 0) {
		cell.particles[s] = 0;
	} else if (cell.particles[s] == 0) {
		cell.particles[s] = 1;
	}
}

void neutral_part(const SpeciesTable &table, std::vector<SpeciesAmount> &amounts) {
	const auto charge_of = [&](const SpeciesAmount &m) { return static_cast<int64_t>(table.species(m.species).charge); };
	int64_t net = 0;
	for (const SpeciesAmount &m : amounts) {
		net += charge_of(m) * m.amount.quanta;
	}
	// each round takes ions off the side in excess; a round that cannot fit the excess takes one ion of the smallest
	// charge, which leaves a smaller excess on the other side; bounded, then nothing charged
	for (int round = 0; round < 16 && net != 0; ++round) {
		const int64_t side = net > 0 ? 1 : -1, excess = net * side;
		SpeciesAmount *fits = nullptr, *least = nullptr;
		for (SpeciesAmount &m : amounts) {
			const int64_t z = charge_of(m) * side;
			if (z <= 0 || m.amount.quanta == 0) {
				continue;
			}
			if (z <= excess && (fits == nullptr || z > charge_of(*fits) * side || (z == charge_of(*fits) * side && m.amount.quanta > fits->amount.quanta))) {
				fits = &m;
			}
			if (least == nullptr || z < charge_of(*least) * side || (z == charge_of(*least) * side && m.amount.quanta > least->amount.quanta)) {
				least = &m;
			}
		}
		if (least == nullptr) {
			break;
		}
		SpeciesAmount &take = fits != nullptr ? *fits : *least;
		const int64_t z = charge_of(take) * side;
		const int64_t n = fits != nullptr ? std::min(excess / z, take.amount.quanta) : 1;
		take.amount.quanta -= n;
		net -= side * z * n;
	}
	if (net != 0) {
		for (SpeciesAmount &m : amounts) {
			if (charge_of(m) != 0) {
				m.amount.quanta = 0;
			}
		}
	}
}

Refusal World::transfer(EdgeId edge, CellId from, std::span<const SpeciesAmount> amounts, Energy energy) {
	if (edge.index < 0 || static_cast<size_t>(edge.index) >= edges_.size() || !edges_[static_cast<size_t>(edge.index)].open) {
		return Refusal::UnknownEdge;
	}
	const Edge &e = edges_[static_cast<size_t>(edge.index)];
	if (!(from == e.a) && !(from == e.b)) {
		return Refusal::NotOnEdge;
	}
	const CellId to = from == e.a ? e.b : e.a;
	CellState &src = cells_[static_cast<size_t>(from.index)];
	CellState &dst = cells_[static_cast<size_t>(to.index)];
	const bool reservoir = meta_[static_cast<size_t>(from.index)].reservoir || meta_[static_cast<size_t>(to.index)].reservoir;

	// Validate everything before changing anything. A species may be listed
	// more than once: its amounts add up.
	std::vector<SpeciesAmount> moved;
	moved.reserve(amounts.size());
	for (const SpeciesAmount &item : amounts) {
		if (!table_->has(item.species)) {
			return Refusal::UnknownSpecies;
		}
		if (item.amount.quanta < 0) {
			return Refusal::NegativeAmount;
		}
		if (item.amount.quanta == 0) {
			continue;
		}
		if (reservoir) {
			return Refusal::Reservoir;
		}
		if (table_->species(item.species).wall) {
			return Refusal::Wall;
		}
		if ((e.phases & phase_bit(table_->species(item.species).phase)) == 0) {
			return Refusal::PhaseNotAllowed;
		}
		bool merged = false;
		for (SpeciesAmount &m : moved) {
			if (m.species == item.species) {
				if (add_overflows(m.amount.quanta, item.amount.quanta)) {
					return Refusal::Overflow;
				}
				m.amount.quanta += item.amount.quanta;
				merged = true;
				break;
			}
		}
		if (!merged) {
			moved.push_back(item);
		}
	}
	neutral_part(*table_, moved);
	std::erase_if(moved, [](const SpeciesAmount &m) { return m.amount.quanta == 0; });
	for (const SpeciesAmount &m : moved) {
		const auto s = static_cast<size_t>(m.species.index);
		if (src.amount[s] < m.amount.quanta) {
			return Refusal::Insufficient;
		}
		if (add_overflows(dst.amount[s], m.amount.quanta)) {
			return Refusal::Overflow;
		}
	}
	if (energy.quanta == std::numeric_limits<int64_t>::min() || add_overflows(src.energy, -energy.quanta) || add_overflows(dst.energy, energy.quanta)) {
		return Refusal::Overflow;
	}
	if (moved.empty() && energy.quanta == 0) {
		return Refusal::None; // nothing to move, nothing to record
	}

	for (const SpeciesAmount &m : moved) {
		const auto s = static_cast<size_t>(m.species.index);
		// a solid's particles go with it, in proportion: at least one with what
		// moves, at least one left with what stays
		const SpeciesDef &def = table_->species(m.species);
		if (def.phase == Phase::Solid && !def.wall && src.particles[s] > 0) {
			int64_t go = src.particles[s];
			if (m.amount.quanta < src.amount[s]) {
				go = static_cast<int64_t>(static_cast<double>(src.particles[s]) * (static_cast<double>(m.amount.quanta) / static_cast<double>(src.amount[s])));
				go = go < 1 ? 1 : go;
				src.particles[s] = src.particles[s] - go < 1 ? 1 : src.particles[s] - go;
			} else {
				src.particles[s] = 0;
			}
			dst.particles[s] += go;
		}
		src.amount[s] -= m.amount.quanta;
		dst.amount[s] += m.amount.quanta;
		tend_particles(src, m.species);
		tend_particles(dst, m.species);
	}
	src.energy -= energy.quanta;
	dst.energy += energy.quanta;
	ledger_.push_back(LedgerEntry{ LedgerEntry::Kind::Transfer, edge, from, to, std::move(moved), energy, 0 });
	return Refusal::None;
}

Refusal World::transform(CellId cell, const Reaction &reaction, int64_t extent) {
	if (!known(cell)) {
		return Refusal::UnknownCell;
	}
	if (reaction.table() != table_) {
		return Refusal::ForeignReaction;
	}
	if (meta_[static_cast<size_t>(cell.index)].reservoir) {
		return Refusal::Reservoir;
	}
	if (extent == 0) {
		return Refusal::None;
	}
	CellState &state = cells_[static_cast<size_t>(cell.index)];
	std::vector<SpeciesAmount> delta;
	delta.reserve(reaction.terms().size());
	for (const Term &t : reaction.terms()) {
		if (mul_overflows(t.nu, extent)) {
			return Refusal::Overflow;
		}
		const int64_t d = static_cast<int64_t>(t.nu) * extent;
		const int64_t have = state.amount[static_cast<size_t>(t.species.index)];
		if (add_overflows(have, d)) {
			return Refusal::Overflow;
		}
		if (have + d < 0) {
			return Refusal::Insufficient;
		}
		delta.push_back(SpeciesAmount{ t.species, Amount{ d } });
	}
	for (const SpeciesAmount &d : delta) {
		state.amount[static_cast<size_t>(d.species.index)] += d.amount.quanta;
		tend_particles(state, d.species);
	}
	ledger_.push_back(LedgerEntry{ LedgerEntry::Kind::Transform, EdgeId{}, cell, CellId{}, std::move(delta), Energy{}, extent });
	return Refusal::None;
}

Refusal World::connect(CellId a, CellId b, PhaseMask phases, EdgeId *made, double conductance) {
	if (!good_conductance(conductance)) {
		return Refusal::BadValue;
	}
	if (!known(a) || !known(b)) {
		return Refusal::UnknownCell;
	}
	if (a == b) {
		return Refusal::SameCell;
	}
	edges_.push_back(Edge{ a, b, phases, true, conductance });
	++topology_version_;
	refresh_sealed();
	if (made != nullptr) {
		*made = EdgeId{ static_cast<int32_t>(edges_.size() - 1) };
	}
	return Refusal::None;
}

CellId World::add_layer_under(CellId lower, Refusal *why) {
	const auto refuse = [&](Refusal r) {
		if (why != nullptr) {
			*why = r;
		}
		return CellId{};
	};
	// (between steps, a command's transfers already in the step's ledger: an empty cell changes no total, and the
	// step's trace starts with it empty; never inside a step's processes)
	if (!known(lower)) {
		return refuse(Refusal::UnknownCell);
	}
	const CellMeta &over = meta_[static_cast<size_t>(lower.index)];
	if (!over.layer && !(over.column_volume > 0.0)) {
		return refuse(Refusal::BadValue); // a column grows under its top or its last layer
	}
	const auto species = static_cast<size_t>(table_->species_count());
	names_.push_back(names_[static_cast<size_t>(lower.index)] + "+");
	CellMeta m;
	m.layer = true;
	meta_.push_back(m);
	const CellState none{ std::vector<int64_t>(species, 0), 0, std::vector<int64_t>(species, 0) };
	cells_.push_back(none);
	before_.cells.push_back(none); // (the step's trace starts with it, empty)
	const CellId made{ static_cast<int32_t>(cells_.size() - 1) };
	Edge e{ made, lower, PHASES_ALL, true, 0.0 };
	e.column = true;
	edges_.push_back(e);
	++topology_version_;
	if (why != nullptr) {
		*why = Refusal::None;
	}
	return made;
}

CellId World::add_cell(const std::string &name, const CellMeta &meta, Refusal *why) {
	const auto refuse = [&](Refusal r) {
		if (why != nullptr) {
			*why = r;
		}
		return CellId{};
	};
	if (!ledger_.empty()) {
		return refuse(Refusal::MidStep);
	}
	if (meta.layer || meta.retired || meta.frozen || (meta.rigid && !(meta.volume > 0.0))) {
		return refuse(Refusal::BadValue); // (a layer grows under its column; a rigid cell has a volume)
	}
	const auto species = static_cast<size_t>(table_->species_count());
	names_.push_back(name);
	meta_.push_back(meta);
	const CellState none{ std::vector<int64_t>(species, 0), 0, std::vector<int64_t>(species, 0) };
	cells_.push_back(none);
	before_.cells.push_back(none); // (the step's trace starts with it, empty: no total changes)
	++topology_version_;
	if (why != nullptr) {
		*why = Refusal::None;
	}
	return CellId{ static_cast<int32_t>(cells_.size() - 1) };
}

Refusal World::supply(CellId cell, const CellState &state) {
	if (!ledger_.empty()) {
		return Refusal::MidStep;
	}
	if (!known(cell)) {
		return Refusal::UnknownCell;
	}
	const auto c = static_cast<size_t>(cell.index);
	const auto species = static_cast<size_t>(table_->species_count());
	if (meta_[c].reservoir || meta_[c].frozen || meta_[c].retired || state.amount.size() != species || (!state.particles.empty() && state.particles.size() != species)) {
		return Refusal::BadValue;
	}
	for (const int64_t a : cells_[c].amount) {
		if (a != 0) {
			return Refusal::BadValue; // (into an empty cell only: what is supplied is the whole of it)
		}
	}
	if (cells_[c].energy != 0) {
		return Refusal::BadValue;
	}
	int64_t charge = 0;
	for (size_t k = 0; k < state.amount.size(); ++k) {
		if (state.amount[k] < 0) {
			return Refusal::NegativeAmount;
		}
		charge += static_cast<int64_t>(table_->species(SpeciesId{ static_cast<int32_t>(k) }).charge) * state.amount[k];
	}
	if (charge != 0) {
		return Refusal::BadValue; // (Stage 63) what comes in is neutral matter, as every cell is
	}
	CellState whole = state;
	if (whole.particles.empty()) {
		whole.particles.assign(species, 0);
	}
	cells_[c] = whole;
	before_.cells[c] = whole; // (between steps: the next step's trace starts with it)
	++topology_version_;
	return Refusal::None;
}

Refusal World::retire(CellId cell, CellState *left) {
	if (!ledger_.empty()) {
		return Refusal::MidStep;
	}
	if (!known(cell)) {
		return Refusal::UnknownCell;
	}
	const auto c = static_cast<size_t>(cell.index);
	if (meta_[c].reservoir || meta_[c].retired) {
		return Refusal::BadValue;
	}
	if (left != nullptr) {
		*left = cells_[c];
	}
	for (Edge &e : edges_) {
		if (e.open && (e.a == cell || e.b == cell)) {
			e.open = false;
		}
		if (e.a == cell || e.b == cell) {
			e.frozen = false; // (closed for good)
		}
	}
	const auto species = static_cast<size_t>(table_->species_count());
	const CellState none{ std::vector<int64_t>(species, 0), 0, std::vector<int64_t>(species, 0) };
	cells_[c] = none;
	before_.cells[c] = none;
	meta_[c].retired = true;
	meta_[c].frozen = true;
	refresh_sealed();
	++topology_version_;
	return Refusal::None;
}

Refusal World::freeze(CellId cell, bool on) {
	if (!ledger_.empty()) {
		return Refusal::MidStep;
	}
	if (!known(cell)) {
		return Refusal::UnknownCell;
	}
	const auto c = static_cast<size_t>(cell.index);
	if (meta_[c].reservoir || meta_[c].retired) {
		return Refusal::BadValue;
	}
	if (meta_[c].frozen == on) {
		return Refusal::None;
	}
	meta_[c].frozen = on;
	for (Edge &e : edges_) {
		if (!(e.a == cell || e.b == cell)) {
			continue;
		}
		if (on && e.open) {
			e.open = false;
			e.frozen = true;
		} else if (!on && e.frozen) {
			const CellId other = e.a == cell ? e.b : e.a;
			if (!meta_[static_cast<size_t>(other.index)].frozen) {
				e.open = true;
				e.frozen = false;
			}
		}
	}
	refresh_sealed();
	++topology_version_;
	return Refusal::None;
}

Refusal World::connect_drain(CellId from, CellId to, EdgeId *made) {
	EdgeId edge;
	const Refusal refused = connect(from, to, PHASES_LIQUID, &edge);
	if (refused != Refusal::None) {
		return refused;
	}
	edges_[static_cast<size_t>(edge.index)].drain = true;
	if (made != nullptr) {
		*made = edge;
	}
	return Refusal::None;
}

Refusal World::set_drain(EdgeId edge, double rate, double carried, double solids_rate) {
	if (!ledger_.empty()) {
		return Refusal::MidStep;
	}
	if (edge.index < 0 || static_cast<size_t>(edge.index) >= edges_.size() || !edges_[static_cast<size_t>(edge.index)].open || !edges_[static_cast<size_t>(edge.index)].drain) {
		return Refusal::UnknownEdge;
	}
	Edge &e = edges_[static_cast<size_t>(edge.index)];
	e.rate = rate;
	e.carried = carried > 0.0 ? (carried < 1.0 ? carried : 1.0) : 0.0;
	e.solids_rate = solids_rate > 0.0 ? solids_rate : 0.0;
	e.phases = e.carried > 0.0 || e.solids_rate > 0.0 ? static_cast<PhaseMask>(PHASES_LIQUID | phase_bit(Phase::Solid)) : PHASES_LIQUID;
	return Refusal::None;
}

Refusal World::disconnect(EdgeId edge) {
	if (!ledger_.empty()) {
		return Refusal::MidStep;
	}
	if (edge.index < 0 || static_cast<size_t>(edge.index) >= edges_.size() || !edges_[static_cast<size_t>(edge.index)].open) {
		return Refusal::UnknownEdge;
	}
	edges_[static_cast<size_t>(edge.index)].open = false;
	const std::vector<CellMeta> was = meta_;
	refresh_sealed();
	// a cell is sealed at the room's pressure: full, as every open cell is at a step's end (a starved one is not)
	for (size_t c = 0; c < meta_.size() && thermal_; ++c) {
		if (!meta_[c].sealed || was[c].sealed) {
			continue;
		}
		bool defined = false;
		const double excess = gas_excess(*table_, was[c], cells_[c], defined);
		double kelvin = 0.0;
		temperature(CellId{ static_cast<int32_t>(c) }, kelvin);
		const double tolerance = static_cast<double>(table_->species_count() + 1) * MOL_PER_QUANTUM * GAS_CONSTANT * kelvin / P_ATMOSPHERE + 1e-9 * meta_[c].volume;
		if (defined && (excess > tolerance || excess < -tolerance)) {
			edges_[static_cast<size_t>(edge.index)].open = true;
			meta_ = was;
			return Refusal::WouldSeal;
		}
	}
	++topology_version_;
	return Refusal::None;
}

Refusal World::set_conductance(EdgeId edge, double conductance) {
	if (!ledger_.empty()) {
		return Refusal::MidStep;
	}
	if (edge.index < 0 || static_cast<size_t>(edge.index) >= edges_.size() || !edges_[static_cast<size_t>(edge.index)].open) {
		return Refusal::UnknownEdge;
	}
	if (!good_conductance(conductance)) {
		return Refusal::BadValue;
	}
	edges_[static_cast<size_t>(edge.index)].conductance = conductance;
	return Refusal::None;
}

Refusal World::set_power(EdgeId edge, double power) {
	if (!ledger_.empty()) {
		return Refusal::MidStep;
	}
	if (edge.index < 0 || static_cast<size_t>(edge.index) >= edges_.size() || !edges_[static_cast<size_t>(edge.index)].open) {
		return Refusal::UnknownEdge;
	}
	if (!(power == power) || power > 1.0e12 || power < -1.0e12) {
		return Refusal::BadValue;
	}
	edges_[static_cast<size_t>(edge.index)].power = power;
	return Refusal::None;
}

Refusal World::set_exchange(EdgeId edge, double exchange) {
	if (!ledger_.empty()) {
		return Refusal::MidStep;
	}
	if (edge.index < 0 || static_cast<size_t>(edge.index) >= edges_.size() || !edges_[static_cast<size_t>(edge.index)].open) {
		return Refusal::UnknownEdge;
	}
	if (!(exchange >= 0.0) || exchange > 1.0e6) {
		return Refusal::BadValue;
	}
	edges_[static_cast<size_t>(edge.index)].exchange = exchange;
	return Refusal::None;
}

void World::refresh_sealed() {
	for (CellMeta &m : meta_) {
		m.sealed = false;
	}
	for (const CellId c : sealed_cells(meta_, edges_)) {
		meta_[static_cast<size_t>(c.index)].sealed = true;
	}
}

void World::skip_step() {
	ledger_.clear();
	++step_;
}

void World::rebase() {
	before_.cells = cells_;
}

StepTrace World::close_step() {
	StepTrace trace;
	close_step(trace);
	return trace;
}

void World::close_step(StepTrace &trace) {
	trace.step = step_;
	// the step's start into the trace, and the trace's old start back as the buffer the next start is copied into
	std::swap(trace.before, before_);
	trace.after.cells = cells_; // copy-assigned: the old buffers kept
	std::swap(trace.ledger, ledger_);
	trace.edges = edges_;
	trace.meta = meta_;
	trace.thermal = thermal_;
	trace.starved.clear();
	trace.exchanged.clear();
	ledger_.clear();
	before_.cells = cells_;
	++step_;
}

} // namespace chem
