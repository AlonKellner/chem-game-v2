#include "chem/world.hpp"

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
			return "a conductance must be finite and not negative";
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

CellId World::Builder::add_cell(std::string name) {
	names_.push_back(std::move(name));
	meta_.push_back(CellMeta{});
	return CellId{ static_cast<int32_t>(names_.size() - 1) };
}

CellId World::Builder::add_reservoir(std::string name, double temperature) {
	names_.push_back(std::move(name));
	meta_.push_back(CellMeta{ true, temperature });
	return CellId{ static_cast<int32_t>(names_.size() - 1) };
}

EdgeId World::Builder::add_edge(CellId a, CellId b, PhaseMask phases, double conductance) {
	edges_.push_back(Edge{ a, b, phases, true, conductance });
	return EdgeId{ static_cast<int32_t>(edges_.size() - 1) };
}

World::Builder &World::Builder::set_temperature(CellId cell, double temperature) {
	temperatures_.push_back(SetTemperature{ cell, temperature });
	return *this;
}

World::Builder &World::Builder::set_amount(CellId cell, SpeciesId species, Amount amount) {
	amounts_.push_back(Set{ cell, species, amount });
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
	world.cells_.assign(names_.size(), CellState{ std::vector<int64_t>(static_cast<size_t>(table_->species_count()), 0), 0 });
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
		world.cells_[static_cast<size_t>(s.cell.index)].amount[static_cast<size_t>(s.species.index)] = s.amount.quanta;
	}
	for (const SetEnergy &s : energies_) {
		if (!known_cell(s.cell)) {
			return refuse("an energy is set in a cell the world does not have");
		}
		world.cells_[static_cast<size_t>(s.cell.index)].energy = s.energy.quanta;
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
		if (!thermal_state(*table_, cell).defined) {
			return refuse("a temperature is set in a cell that cannot have one (a species without thermodynamic data, or no heat capacity)");
		}
		bool ok = false;
		const double joule = enthalpy_at(*table_, cell, s.temperature, ok);
		const Energy energy = energy_from_joule(joule, ok);
		if (!ok) {
			return refuse("a cell's enthalpy does not fit the integer range");
		}
		cell.energy = energy.quanta;
		world.thermal_ = true;
	}
	if (world.thermal_) {
		for (size_t c = 0; c < world.cells_.size(); ++c) {
			if (!meta_[c].reservoir && !thermal_state(*table_, world.cells_[c]).defined) {
				return refuse("in a world with temperatures every cell has one (a species without thermodynamic data, or no heat capacity)");
			}
		}
	}
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
	const Thermal t = thermal_state(*table_, cells_[static_cast<size_t>(cell.index)]);
	kelvin = t.temperature;
	return t.defined;
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
		src.amount[s] -= m.amount.quanta;
		dst.amount[s] += m.amount.quanta;
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
	}
	ledger_.push_back(LedgerEntry{ LedgerEntry::Kind::Transform, EdgeId{}, cell, CellId{}, std::move(delta), Energy{}, extent });
	return Refusal::None;
}

Refusal World::connect(CellId a, CellId b, PhaseMask phases, EdgeId *made, double conductance) {
	if (!ledger_.empty()) {
		return Refusal::MidStep;
	}
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
	if (made != nullptr) {
		*made = EdgeId{ static_cast<int32_t>(edges_.size() - 1) };
	}
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

StepTrace World::close_step() {
	StepTrace trace;
	trace.step = step_;
	trace.before = std::move(before_);
	trace.after = Snapshot{ cells_ };
	trace.ledger = std::move(ledger_);
	trace.edges = edges_;
	trace.meta = meta_;
	trace.thermal = thermal_;
	ledger_.clear();
	before_ = trace.after;
	++step_;
	return trace;
}

} // namespace chem
