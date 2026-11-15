#include "chem/invariants.hpp"

#include "chem/thermo.hpp"

#include <cstdio>

namespace chem {

namespace {

constexpr size_t MAX_MESSAGES = 8; // per check: the first few say enough

std::string text(const char *format, long long a = 0, long long b = 0, long long c = 0, long long d = 0) {
	char buffer[256];
	std::snprintf(buffer, sizeof buffer, format, a, b, c, d);
	return buffer;
}

// wrapping arithmetic: a corrupted trace is compared, never undefined
int64_t wrap_add(int64_t a, int64_t b) {
	return static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}
int64_t wrap_mul(int64_t a, int64_t b) {
	return static_cast<int64_t>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
}

bool well_formed(const SpeciesTable &table, const Snapshot &s) {
	for (const CellState &c : s.cells) {
		if (c.amount.size() != static_cast<size_t>(table.species_count())) {
			return false;
		}
	}
	return true;
}

bool well_formed(const SpeciesTable &table, const StepTrace &trace) {
	return trace.before.cells.size() == trace.after.cells.size() && well_formed(table, trace.before) && well_formed(table, trace.after) &&
			(!trace.thermal || trace.meta.size() == trace.after.cells.size());
}

bool finite(double v) {
	return v - v == 0.0;
}

// A cell's temperature as the trace has it: a reservoir's fixed one, else
// derived from the state. false when it has none.
bool temperature_of(const SpeciesTable &table, const StepTrace &trace, const Snapshot &state, size_t cell, double &kelvin) {
	if (trace.meta[cell].reservoir) {
		kelvin = trace.meta[cell].reservoir_temperature;
		return true;
	}
	const Thermal t = thermal_state(table, state.cells[cell]);
	kelvin = t.temperature;
	return t.defined;
}

// A cell's entropy in J/K: a reservoir's is its energy over its temperature.
// false when the cell has no positive finite temperature.
bool entropy_of(const SpeciesTable &table, const StepTrace &trace, const Snapshot &state, size_t cell, double &s) {
	double kelvin = 0.0;
	if (!temperature_of(table, trace, state, cell, kelvin) || !(kelvin > 0.0) || !finite(kelvin)) {
		return false;
	}
	s = trace.meta[cell].reservoir ? static_cast<double>(state.cells[cell].energy) * JOULE_PER_QUANTUM / kelvin : entropy(table, state.cells[cell], kelvin);
	return true;
}

// sum of (S_after - S_before) and of |S| over the cells that changed
void entropy_sums(const SpeciesTable &table, const StepTrace &trace, double &change, double &magnitude) {
	change = 0.0;
	magnitude = 0.0;
	for (size_t c = 0; c < trace.after.cells.size(); ++c) {
		if (trace.before.cells[c] == trace.after.cells[c]) {
			continue;
		}
		double before = 0.0, after = 0.0;
		if (!entropy_of(table, trace, trace.before, c, before) || !entropy_of(table, trace, trace.after, c, after)) {
			continue; // T3 reports a cell without a temperature
		}
		change += after - before;
		magnitude += (before < 0.0 ? -before : before) + (after < 0.0 ? -after : after);
	}
}

bool cell_in(const StepTrace &trace, CellId c) {
	return c.index >= 0 && static_cast<size_t>(c.index) < trace.after.cells.size();
}

} // namespace

Totals totals(const SpeciesTable &table, const Snapshot &snapshot) {
	Totals t;
	for (const CellState &cell : snapshot.cells) {
		for (size_t s = 0; s < cell.amount.size() && s < static_cast<size_t>(table.species_count()); ++s) {
			const SpeciesId id{ static_cast<int32_t>(s) };
			const SpeciesDef &def = table.species(id);
			const int64_t n = cell.amount[s];
			for (size_t e = 0; e < t.atoms.size(); ++e) {
				t.atoms[e] = wrap_add(t.atoms[e], wrap_mul(n, def.atoms[e]));
			}
			t.charge = wrap_add(t.charge, wrap_mul(n, def.charge));
			t.mass.add(n, table.molar_mass_ug(id));
		}
		t.energy = wrap_add(t.energy, cell.energy);
	}
	return t;
}

bool totals_fit(const SpeciesTable &table, const Snapshot &snapshot) {
	std::array<int64_t, MAX_ELEMENTS> atoms{};
	int64_t charge = 0;
	int64_t energy = 0;
	for (const CellState &cell : snapshot.cells) {
		for (size_t s = 0; s < cell.amount.size() && s < static_cast<size_t>(table.species_count()); ++s) {
			const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
			const int64_t n = cell.amount[s];
			for (size_t e = 0; e < atoms.size(); ++e) {
				if (mul_overflows(n, def.atoms[e]) || add_overflows(atoms[e], n * def.atoms[e])) {
					return false;
				}
				atoms[e] += n * def.atoms[e];
			}
			if (mul_overflows(n, def.charge) || add_overflows(charge, n * def.charge)) {
				return false;
			}
			charge += n * def.charge;
		}
		if (add_overflows(energy, cell.energy)) {
			return false;
		}
		energy += cell.energy;
	}
	return true;
}

Violations check_C1(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	const Totals a = totals(table, trace.before);
	const Totals b = totals(table, trace.after);
	for (int e = 0; e < table.element_count(); ++e) {
		const auto i = static_cast<size_t>(e);
		if (a.atoms[i] != b.atoms[i]) {
			out.push_back("C1: step " + std::to_string(trace.step) + ": element " + std::string(table.element(e).symbol) +
					text(" changed from %lld to %lld quanta of atoms", a.atoms[i], b.atoms[i]));
		}
	}
	return out;
}

Violations check_C2(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	const Totals a = totals(table, trace.before);
	const Totals b = totals(table, trace.after);
	if (!(a.mass == b.mass)) {
		out.push_back(text("C2: step %lld: the world's mass changed (limbs %lld:%lld", static_cast<long long>(trace.step), a.mass.hi, a.mass.lo) +
				text(" to %lld:%lld)", b.mass.hi, b.mass.lo));
	}
	return out;
}

Violations check_C3(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	const Totals a = totals(table, trace.before);
	const Totals b = totals(table, trace.after);
	if (a.energy != b.energy) {
		out.push_back(text("C3: step %lld: the world's energy changed from %lld to %lld quanta", static_cast<long long>(trace.step), a.energy, b.energy));
	}
	return out;
}

Violations check_C4(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	const Totals a = totals(table, trace.before);
	const Totals b = totals(table, trace.after);
	if (a.charge != b.charge) {
		out.push_back(text("C4: step %lld: the world's charge changed from %lld to %lld quanta", static_cast<long long>(trace.step), a.charge, b.charge));
	}
	return out;
}

Violations check_C5(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	Snapshot replay = trace.before;
	const auto species = static_cast<size_t>(table.species_count());
	for (size_t i = 0; i < trace.ledger.size(); ++i) {
		const LedgerEntry &entry = trace.ledger[i];
		const bool transfer = entry.kind == LedgerEntry::Kind::Transfer;
		if (!cell_in(trace, entry.from) || (transfer && !cell_in(trace, entry.to))) {
			if (out.size() < MAX_MESSAGES) {
				out.push_back(text("C5: step %lld: ledger entry %lld names a cell the world does not have", static_cast<long long>(trace.step), static_cast<long long>(i)));
			}
			continue;
		}
		CellState &from = replay.cells[static_cast<size_t>(entry.from.index)];
		for (const SpeciesAmount &d : entry.delta) {
			if (d.species.index < 0 || static_cast<size_t>(d.species.index) >= species) {
				if (out.size() < MAX_MESSAGES) {
					out.push_back(text("C5: step %lld: ledger entry %lld names a species the table does not have", static_cast<long long>(trace.step), static_cast<long long>(i)));
				}
				continue;
			}
			const auto s = static_cast<size_t>(d.species.index);
			if (transfer) {
				CellState &to = replay.cells[static_cast<size_t>(entry.to.index)];
				from.amount[s] = wrap_add(from.amount[s], -d.amount.quanta);
				to.amount[s] = wrap_add(to.amount[s], d.amount.quanta);
			} else {
				from.amount[s] = wrap_add(from.amount[s], d.amount.quanta);
			}
		}
		if (transfer) {
			CellState &to = replay.cells[static_cast<size_t>(entry.to.index)];
			from.energy = wrap_add(from.energy, -entry.energy.quanta);
			to.energy = wrap_add(to.energy, entry.energy.quanta);
		}
	}
	for (size_t c = 0; c < replay.cells.size(); ++c) {
		for (size_t s = 0; s < species; ++s) {
			if (replay.cells[c].amount[s] != trace.after.cells[c].amount[s] && out.size() < MAX_MESSAGES) {
				out.push_back("C5: step " + std::to_string(trace.step) + ": cell " + std::to_string(c) + ", species " +
						std::string(table.species(SpeciesId{ static_cast<int32_t>(s) }).id) +
						text(": the ledger gives %lld quanta, the state has %lld", replay.cells[c].amount[s], trace.after.cells[c].amount[s]));
			}
		}
		if (replay.cells[c].energy != trace.after.cells[c].energy && out.size() < MAX_MESSAGES) {
			out.push_back(text("C5: step %lld: cell %lld: the ledger gives %lld quanta of energy, the state has %lld", static_cast<long long>(trace.step),
					static_cast<long long>(c), replay.cells[c].energy, trace.after.cells[c].energy));
		}
	}
	return out;
}

Violations check_C6(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	for (size_t c = 0; c < trace.after.cells.size(); ++c) {
		for (size_t s = 0; s < trace.after.cells[c].amount.size(); ++s) {
			if (trace.after.cells[c].amount[s] < 0 && out.size() < MAX_MESSAGES) {
				out.push_back("C6: step " + std::to_string(trace.step) + ": cell " + std::to_string(c) + " holds a negative amount of " +
						std::string(table.species(SpeciesId{ static_cast<int32_t>(s) }).id) + text(" (%lld quanta)", trace.after.cells[c].amount[s]));
			}
		}
	}
	return out;
}

Violations check_C7(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	for (size_t i = 0; i < trace.ledger.size() && out.size() < MAX_MESSAGES; ++i) {
		const LedgerEntry &entry = trace.ledger[i];
		if (entry.kind != LedgerEntry::Kind::Transfer) {
			continue;
		}
		const auto step = static_cast<long long>(trace.step);
		const auto index = static_cast<long long>(i);
		if (entry.edge.index < 0 || static_cast<size_t>(entry.edge.index) >= trace.edges.size()) {
			out.push_back(text("C7: step %lld: transfer %lld runs along no edge of the world", step, index));
			continue;
		}
		const Edge &edge = trace.edges[static_cast<size_t>(entry.edge.index)];
		if (!edge.open) {
			out.push_back(text("C7: step %lld: transfer %lld runs along a disconnected edge", step, index));
			continue;
		}
		const bool forward = entry.from == edge.a && entry.to == edge.b;
		const bool backward = entry.from == edge.b && entry.to == edge.a;
		if (!forward && !backward) {
			out.push_back(text("C7: step %lld: transfer %lld is not between the two cells of its edge", step, index));
			continue;
		}
		for (const SpeciesAmount &d : entry.delta) {
			if (!table.has(d.species)) {
				out.push_back(text("C7: step %lld: transfer %lld moves a species the table does not have", step, index));
			} else if (d.amount.quanta < 0) {
				out.push_back(text("C7: step %lld: transfer %lld moves a negative amount", step, index));
			} else if (d.amount.quanta > 0 && (edge.phases & phase_bit(table.species(d.species).phase)) == 0) {
				out.push_back(text("C7: step %lld: transfer %lld moves ", step, index) + std::string(table.species(d.species).id) +
						" along an edge that does not let its phase through");
			}
		}
	}
	return out;
}

Violations check_C8(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	for (size_t i = 0; i < trace.ledger.size() && out.size() < MAX_MESSAGES; ++i) {
		const LedgerEntry &entry = trace.ledger[i];
		if (entry.kind != LedgerEntry::Kind::Transform) {
			continue;
		}
		std::array<int64_t, MAX_ELEMENTS> atoms{};
		int64_t charge = 0;
		bool known = true;
		for (const SpeciesAmount &d : entry.delta) {
			if (!table.has(d.species)) {
				known = false;
				break;
			}
			const SpeciesDef &def = table.species(d.species);
			for (size_t e = 0; e < atoms.size(); ++e) {
				atoms[e] = wrap_add(atoms[e], wrap_mul(d.amount.quanta, def.atoms[e]));
			}
			charge = wrap_add(charge, wrap_mul(d.amount.quanta, def.charge));
		}
		bool zero = known && charge == 0;
		for (const int64_t a : atoms) {
			zero = zero && a == 0;
		}
		if (!zero) {
			out.push_back(text("C8: step %lld: transform %lld is not balanced in every element and in charge", static_cast<long long>(trace.step), static_cast<long long>(i)));
		}
	}
	return out;
}

Violations check_T1(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	if (!trace.thermal) {
		return out;
	}
	Snapshot replay = trace.before;
	for (size_t i = 0; i < trace.ledger.size(); ++i) {
		const LedgerEntry &entry = trace.ledger[i];
		const bool transfer = entry.kind == LedgerEntry::Kind::Transfer;
		if (!cell_in(trace, entry.from) || (transfer && !cell_in(trace, entry.to))) {
			continue; // C5 reports it
		}
		const auto from = static_cast<size_t>(entry.from.index);
		if (transfer && entry.delta.empty() && entry.energy.quanta != 0) {
			const auto to = static_cast<size_t>(entry.to.index);
			double t_from = 0.0, t_to = 0.0;
			if (temperature_of(table, trace, replay, from, t_from) && temperature_of(table, trace, replay, to, t_to)) {
				const double drive = t_from - t_to;
				if ((entry.energy.quanta > 0 && drive < 0.0) || (entry.energy.quanta < 0 && drive > 0.0) || drive == 0.0) {
					if (out.size() < MAX_MESSAGES) {
						char buffer[200];
						std::snprintf(buffer, sizeof buffer, "T1: step %lld: transfer %lld moves %lld quanta of heat from a cell at %.9g K to one at %.9g K",
								static_cast<long long>(trace.step), static_cast<long long>(i), static_cast<long long>(entry.energy.quanta), t_from, t_to);
						out.push_back(buffer);
					}
				}
			}
		}
		if (!transfer && entry.extent != 0 && !trace.meta[from].reservoir) {
			// the reaction's numbers back from its change: nu = delta / extent
			std::array<Term, MAX_TERMS> terms{};
			size_t count = 0;
			bool whole = entry.delta.size() <= terms.size();
			for (const SpeciesAmount &d : entry.delta) {
				if (!whole || !table.has(d.species) || d.amount.quanta % entry.extent != 0) {
					whole = false;
					break;
				}
				terms[count++] = Term{ d.species, static_cast<int32_t>(d.amount.quanta / entry.extent) };
			}
			const Thermal thermal = thermal_state(table, replay.cells[from]);
			if (whole && thermal.defined && thermal.temperature > 0.0) {
				bool defined = false;
				const double a = affinity(table, replay.cells[from], std::span<const Term>(terms.data(), count), thermal.temperature, defined);
				if (defined && !((entry.extent > 0 && a > 0.0) || (entry.extent < 0 && a < 0.0)) && out.size() < MAX_MESSAGES) {
					char buffer[200];
					std::snprintf(buffer, sizeof buffer, "T1: step %lld: transform %lld runs %lld quanta against an affinity of %.6g J/mol at %.6g K",
							static_cast<long long>(trace.step), static_cast<long long>(i), static_cast<long long>(entry.extent), a, thermal.temperature);
					out.push_back(buffer);
				}
			}
		}
		// replay the entry
		for (const SpeciesAmount &d : entry.delta) {
			if (!table.has(d.species)) {
				continue;
			}
			const auto s = static_cast<size_t>(d.species.index);
			if (transfer) {
				replay.cells[from].amount[s] = wrap_add(replay.cells[from].amount[s], -d.amount.quanta);
				auto &to_amount = replay.cells[static_cast<size_t>(entry.to.index)].amount[s];
				to_amount = wrap_add(to_amount, d.amount.quanta);
			} else {
				replay.cells[from].amount[s] = wrap_add(replay.cells[from].amount[s], d.amount.quanta);
			}
		}
		if (transfer) {
			replay.cells[from].energy = wrap_add(replay.cells[from].energy, -entry.energy.quanta);
			auto &to_energy = replay.cells[static_cast<size_t>(entry.to.index)].energy;
			to_energy = wrap_add(to_energy, entry.energy.quanta);
		}
	}
	return out;
}

double entropy_change(const SpeciesTable &table, const StepTrace &trace) {
	double change = 0.0, magnitude = 0.0;
	if (trace.thermal && well_formed(table, trace)) {
		entropy_sums(table, trace, change, magnitude);
	}
	return change;
}

Violations check_T2(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	if (!trace.thermal) {
		return out;
	}
	double change = 0.0, magnitude = 0.0;
	entropy_sums(table, trace, change, magnitude);
	const double tolerance = 1e-9 + 4e-16 * magnitude;
	if (!(change >= -tolerance)) {
		char buffer[200];
		std::snprintf(buffer, sizeof buffer, "T2: step %lld: the world's entropy fell by %.6g J/K (tolerance %.3g)", static_cast<long long>(trace.step), -change, tolerance);
		out.push_back(buffer);
	}
	return out;
}

Violations check_T3(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	if (!trace.thermal) {
		return out;
	}
	for (size_t c = 0; c < trace.after.cells.size() && out.size() < MAX_MESSAGES; ++c) {
		if (trace.meta[c].reservoir) {
			continue;
		}
		const Thermal t = thermal_state(table, trace.after.cells[c]);
		if (!t.defined) {
			out.push_back(text("T3: step %lld: cell %lld has no temperature (a species without thermodynamic data, or no heat capacity)",
					static_cast<long long>(trace.step), static_cast<long long>(c)));
		} else if (!(t.temperature > 0.0)) {
			out.push_back(text("T3: step %lld: cell %lld is not above 0 K", static_cast<long long>(trace.step), static_cast<long long>(c)));
		}
	}
	return out;
}

Violations check_S1(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	if (!trace.thermal) {
		return out;
	}
	for (size_t c = 0; c < trace.after.cells.size() && out.size() < MAX_MESSAGES; ++c) {
		double kelvin = 0.0;
		if (!temperature_of(table, trace, trace.after, c, kelvin)) {
			continue; // T3 reports it
		}
		double s = 0.0;
		const bool has_entropy = entropy_of(table, trace, trace.after, c, s);
		if (!finite(kelvin) || (has_entropy && !finite(s))) {
			out.push_back(text("S1: step %lld: cell %lld has a temperature or an entropy that is not finite", static_cast<long long>(trace.step), static_cast<long long>(c)));
		}
	}
	return out;
}

Violations check_all(const SpeciesTable &table, const StepTrace &trace) {
	if (!well_formed(table, trace)) {
		return Violations{ text("SHAPE: step %lld: the trace's cell or species counts disagree", static_cast<long long>(trace.step)) };
	}
	Violations out;
	for (const auto check : { check_C1, check_C2, check_C3, check_C4, check_C5, check_C6, check_C7, check_C8, check_T1, check_T2, check_T3, check_S1 }) {
		Violations v = check(table, trace);
		out.insert(out.end(), v.begin(), v.end());
	}
	return out;
}

} // namespace chem
