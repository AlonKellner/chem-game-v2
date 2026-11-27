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

// the cell's kind as the trace has it (null: a trace built without kinds)
const CellMeta *meta_of(const StepTrace &trace, size_t cell) {
	return cell < trace.meta.size() ? &trace.meta[cell] : nullptr;
}

// the cell's kind for a state of the trace: the step's start at its volume then (Stage 79: a piston moved it)
const CellMeta *meta_of(const StepTrace &trace, const Snapshot &state, size_t cell) {
	const CellMeta *meta = meta_of(trace, cell);
	if (meta == nullptr || &state != &trace.before || cell >= trace.volume_before.size() || trace.volume_before[cell] == meta->volume) {
		return meta;
	}
	thread_local CellMeta before;
	before = *meta;
	before.volume = trace.volume_before[cell];
	return &before;
}

// A cell's temperature as the trace has it: a reservoir's fixed one, else
// derived from the state. false when it has none.
bool temperature_of(const SpeciesTable &table, const StepTrace &trace, const Snapshot &state, size_t cell, double &kelvin) {
	if (trace.meta[cell].reservoir) {
		kelvin = trace.meta[cell].reservoir_temperature;
		return true;
	}
	const Thermal t = thermal_state(table, state.cells[cell], meta_of(trace, state, cell));
	kelvin = t.temperature;
	return t.defined;
}

// The temperature a cell's volumes are read at (Stage 67: its liquids grow with it): its own, or 25 C where it has
// none (a trace without temperatures, or a cell T3 reports)
double volume_kelvin(const SpeciesTable &table, const StepTrace &trace, size_t cell) {
	double kelvin = T0;
	return trace.thermal && cell < trace.meta.size() && temperature_of(table, trace, trace.after, cell, kelvin) && kelvin > 0.0 ? kelvin : T0;
}

// A cell's temperature and entropy in J/K (a reservoir's, its energy over its temperature; none without a positive
// finite temperature), kept for the cell's slot on each thread
// while its table, state and kind are the same (Stage 68): a step's checks ask the entropy of every cell after it
// (S1), and of each changed cell before and after it (T2), and a cell's state before a step is its state after the
// last. Both are functions of the table, the state and the kind alone, so a kept one is the one computed.
struct Thermo {
	bool t_ok = false, s_ok = false;
	double kelvin = 0.0, s = 0.0;
};
const Thermo &thermo_of(const SpeciesTable &table, const StepTrace &trace, const Snapshot &state, size_t cell) {
	struct Kept {
		const SpeciesTable *table = nullptr;
		CellState state;
		CellMeta meta;
		Thermo thermo;
	};
	thread_local std::vector<Kept> kept;
	if (kept.size() <= cell) {
		kept.resize(cell + 1);
	}
	Kept &k = kept[cell];
	const CellState &now = state.cells[cell];
	const CellMeta &meta = *meta_of(trace, state, cell);
	if (k.table == &table && k.state == now && k.meta == meta) {
		return k.thermo;
	}
	Thermo t;
	t.t_ok = temperature_of(table, trace, state, cell, t.kelvin);
	if (t.t_ok && t.kelvin > 0.0 && finite(t.kelvin)) {
		t.s_ok = true;
		t.s = trace.meta[cell].reservoir ? static_cast<double>(now.energy) * JOULE_PER_QUANTUM / t.kelvin : entropy(table, now, t.kelvin, &meta);
	}
	k.table = &table;
	k.state = now;
	k.meta = meta;
	k.thermo = t;
	return k.thermo;
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
		// an empty cell's entropy is none (Stage 47: a column's layer that fills, or empties, in the step)
		const auto none = [](const CellState &x) { return std::all_of(x.amount.begin(), x.amount.end(), [](int64_t a) { return a == 0; }) && x.energy == 0; };
		const auto entropy_at = [&](const Snapshot &state, double &s) {
			const Thermo &t = thermo_of(table, trace, state, c);
			s = t.s_ok ? t.s : s;
			return t.s_ok;
		};
		const bool was = none(trace.before.cells[c]) || entropy_at(trace.before, before);
		const bool is = none(trace.after.cells[c]) || entropy_at(trace.after, after);
		if (!was || !is) {
			continue; // T3 reports a cell without a temperature
		}
		change += after - before;
		magnitude += (before < 0.0 ? -before : before) + (after < 0.0 ? -after : after);
	}
}

// The step's start, to replay its ledger on: one copy a thread, its buffers kept from step to step, so a checked
// step allocates nothing for it (Stage 68). Copy-assigned, so it is the start exactly.
Snapshot &replay_of(const Snapshot &before) {
	thread_local Snapshot scratch;
	scratch.cells = before.cells;
	return scratch;
}

bool cell_in(const StepTrace &trace, CellId c) {
	return c.index >= 0 && static_cast<size_t>(c.index) < trace.after.cells.size();
}

} // namespace

Totals totals(const SpeciesTable &table, const Snapshot &snapshot) {
	Totals t;
	for (const CellState &cell : snapshot.cells) {
		for (size_t s = 0; s < cell.amount.size() && s < static_cast<size_t>(table.species_count()); ++s) {
			const int64_t n = cell.amount[s];
			if (n == 0) {
				continue; // adds nothing (Stage 68: most species are absent from most cells)
			}
			const SpeciesId id{ static_cast<int32_t>(s) };
			const SpeciesDef &def = table.species(id);
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

Totals plus(const Totals &a, const Totals &b) {
	Totals t;
	for (size_t e = 0; e < t.atoms.size(); ++e) {
		t.atoms[e] = wrap_add(a.atoms[e], b.atoms[e]);
	}
	t.charge = wrap_add(a.charge, b.charge);
	t.energy = wrap_add(a.energy, b.energy);
	t.mass.hi = wrap_add(a.mass.hi, b.mass.hi);
	t.mass.lo = a.mass.lo + b.mass.lo; // (each under 2^32: no overflow)
	t.mass.hi = wrap_add(t.mass.hi, t.mass.lo >> 32);
	t.mass.lo &= 0xffffffff;
	return t;
}

Totals minus(const Totals &a, const Totals &b) {
	Totals t;
	for (size_t e = 0; e < t.atoms.size(); ++e) {
		t.atoms[e] = wrap_add(a.atoms[e], wrap_mul(b.atoms[e], -1));
	}
	t.charge = wrap_add(a.charge, wrap_mul(b.charge, -1));
	t.energy = wrap_add(a.energy, wrap_mul(b.energy, -1));
	t.mass.hi = wrap_add(a.mass.hi, wrap_mul(b.mass.hi, -1));
	t.mass.lo = a.mass.lo - b.mass.lo; // (each in [0, 2^32): a borrow at most)
	if (t.mass.lo < 0) {
		t.mass.lo += 4294967296LL;
		t.mass.hi = wrap_add(t.mass.hi, -1);
	}
	return t;
}

Violations check_C9(const Totals &start, const Totals &supplied, const Totals &retired, const Totals &now) {
	Violations out;
	const Totals expected = minus(plus(start, supplied), retired);
	for (size_t e = 0; e < now.atoms.size(); ++e) {
		if (now.atoms[e] != expected.atoms[e]) {
			out.push_back(text("C9: element %lld: %lld quanta of atoms, where its start, what was supplied and what was retired make %lld", static_cast<long long>(e),
					static_cast<long long>(now.atoms[e]), static_cast<long long>(expected.atoms[e])));
		}
	}
	if (!(now.mass == expected.mass)) {
		char buffer[200];
		std::snprintf(buffer, sizeof buffer, "C9: the mass is %.12g g, where its start, what was supplied and what was retired make %.12g g", now.mass.grams(), expected.mass.grams());
		out.push_back(buffer);
	}
	if (now.charge != expected.charge) {
		out.push_back(text("C9: the charge is %lld quanta, where its ledger makes %lld", static_cast<long long>(now.charge), static_cast<long long>(expected.charge)));
	}
	if (now.energy != expected.energy) {
		out.push_back(text("C9: the energy is %lld quanta, where its ledger makes %lld", static_cast<long long>(now.energy), static_cast<long long>(expected.energy)));
	}
	return out;
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

// C1 to C4 on the two totals, computed once a step by check_all (Stage 68)
static Violations c1_of(const SpeciesTable &table, const StepTrace &trace, const Totals &a, const Totals &b);
static Violations c2_of(const SpeciesTable &table, const StepTrace &trace, const Totals &a, const Totals &b);
static Violations c3_of(const SpeciesTable &table, const StepTrace &trace, const Totals &a, const Totals &b);
static Violations c4_of(const SpeciesTable &table, const StepTrace &trace, const Totals &a, const Totals &b);

Violations check_C1(const SpeciesTable &table, const StepTrace &trace) {
	return c1_of(table, trace, totals(table, trace.before), totals(table, trace.after));
}

static Violations c1_of(const SpeciesTable &table, const StepTrace &trace, const Totals &a, const Totals &b) {
	Violations out;
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
	return c2_of(table, trace, totals(table, trace.before), totals(table, trace.after));
}

static Violations c2_of(const SpeciesTable &, const StepTrace &trace, const Totals &a, const Totals &b) {
	Violations out;
	if (!(a.mass == b.mass)) {
		out.push_back(text("C2: step %lld: the world's mass changed (limbs %lld:%lld", static_cast<long long>(trace.step), a.mass.hi, a.mass.lo) +
				text(" to %lld:%lld)", b.mass.hi, b.mass.lo));
	}
	return out;
}

Violations check_C3(const SpeciesTable &table, const StepTrace &trace) {
	return c3_of(table, trace, totals(table, trace.before), totals(table, trace.after));
}

static Violations c3_of(const SpeciesTable &, const StepTrace &trace, const Totals &a, const Totals &b) {
	Violations out;
	if (a.energy != b.energy) {
		out.push_back(text("C3: step %lld: the world's energy changed from %lld to %lld quanta", static_cast<long long>(trace.step), a.energy, b.energy));
	}
	return out;
}

Violations check_C4(const SpeciesTable &table, const StepTrace &trace) {
	return c4_of(table, trace, totals(table, trace.before), totals(table, trace.after));
}

static Violations c4_of(const SpeciesTable &, const StepTrace &trace, const Totals &a, const Totals &b) {
	Violations out;
	if (a.charge != b.charge) {
		out.push_back(text("C4: step %lld: the world's charge changed from %lld to %lld quanta", static_cast<long long>(trace.step), a.charge, b.charge));
	}
	return out;
}

Violations check_N1(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	const auto charge = [&](const CellState &cell) {
		uint64_t q = 0; // wraps modulo 2^64, as the totals do: a corrupted trace is compared, not undefined
		for (size_t s = 0; s < cell.amount.size(); ++s) {
			q += static_cast<uint64_t>(table.species(SpeciesId{ static_cast<int32_t>(s) }).charge) * static_cast<uint64_t>(cell.amount[s]);
		}
		return static_cast<int64_t>(q);
	};
	for (size_t c = 0; c < trace.after.cells.size() && c < trace.before.cells.size() && out.size() < MAX_MESSAGES; ++c) {
		const int64_t a = charge(trace.before.cells[c]), b = charge(trace.after.cells[c]);
		if (a != b) {
			out.push_back(text("N1: step %lld: cell %lld's charge changed from %lld to %lld quanta", static_cast<long long>(trace.step), static_cast<long long>(c), static_cast<long long>(a), static_cast<long long>(b)));
		}
	}
	return out;
}

Violations check_Q1(const SpeciesTable &table, const CellState &cell, double kappa, double limit) {
	Violations out;
	bool ions = false;
	for (size_t s = 0; s < cell.amount.size() && !ions; ++s) {
		const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
		if (cell.amount[s] > 0 && def.phase == Phase::Aqueous) {
			for (const IonCount &ion : def.ions) {
				ions = ions || (ion.count > 0 && table.solution().ions[static_cast<size_t>(ion.ion)].charge != 0);
			}
		}
		// water's own ions are always there where there is water and its ionisation
		ions = ions || (cell.amount[s] > 0 && def.phase == Phase::Liquid && table.solution().ionisation_g > 0.0);
	}
	char buffer[200];
	if (!(kappa >= 0.0) || !(limit >= 0.0)) {
		std::snprintf(buffer, sizeof buffer, "Q1: a conductivity of %g S/m, its limit %g: less than nought, or not a number", kappa, limit);
		out.push_back(buffer);
	} else if (kappa > limit * (1.0 + 1e-12)) {
		std::snprintf(buffer, sizeof buffer, "Q1: a conductivity of %g S/m, past Kohlrausch's limit of %g", kappa, limit);
		out.push_back(buffer);
	} else if (!ions && kappa != 0.0) {
		std::snprintf(buffer, sizeof buffer, "Q1: a conductivity of %g S/m in a cell with no ion", kappa);
		out.push_back(buffer);
	}
	return out;
}

Violations check_C5(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	Snapshot &replay = replay_of(trace.before);
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

// a transfer that is one of the step's exchanges by diffusion (Stage 50): its edge, the cell that gave, its one gas
// and its amount, as the simulation recorded them
static bool exchanged_as(const StepTrace &trace, const LedgerEntry &entry) {
	if (entry.delta.size() != 1) {
		return false;
	}
	for (const Exchanged &x : trace.exchanged) {
		if (x.edge == entry.edge && x.from == entry.from && x.to == entry.to && x.species == entry.delta[0].species && x.moved == entry.delta[0].amount.quanta) {
			return true;
		}
	}
	return false;
}

Violations check_T1(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	if (!trace.thermal) {
		return out;
	}
	Snapshot &replay = replay_of(trace.before);
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
		if (transfer && !entry.delta.empty()) {
			// gas alone: out of an over-full rigid cell, or into an under-full one
			bool gas_only = true;
			for (const SpeciesAmount &d : entry.delta) {
				gas_only = gas_only && table.has(d.species) && table.species(d.species).phase == Phase::Gas;
			}
			const auto to = static_cast<size_t>(entry.to.index);
			if (gas_only && trace.meta[from].sealed && trace.meta[to].sealed) {
				// cells sealed together (Stage 28): from the higher pressure to the lower
				const Thermal a = thermal_state(table, replay.cells[from], &trace.meta[from]), b = thermal_state(table, replay.cells[to], &trace.meta[to]);
				if (a.defined && b.defined && !(a.pressure > b.pressure) && out.size() < MAX_MESSAGES) {
					char buffer[200];
					std::snprintf(buffer, sizeof buffer, "T1: step %lld: transfer %lld moves gas from a sealed cell at %.9g Pa to one at %.9g Pa", static_cast<long long>(trace.step),
							static_cast<long long>(i), a.pressure, b.pressure);
					out.push_back(buffer);
				}
			} else if (gas_only && (trace.meta[from].rigid || trace.meta[to].rigid) &&
					!(entry.edge.index >= 0 && static_cast<size_t>(entry.edge.index) < trace.edges.size() && trace.edges[static_cast<size_t>(entry.edge.index)].column) &&
					!exchanged_as(trace, entry)) {
				// (a bubble rising up a column, Stage 47, is buoyancy, not pressure; a gas exchanged at a mouth, Stage 50,
				// diffusion, which E1 holds)
				bool from_defined = false, to_defined = false;
				const double from_excess = trace.meta[from].rigid ? gas_excess(table, trace.meta[from], replay.cells[from], from_defined) : 0.0;
				const double to_excess = trace.meta[to].rigid ? gas_excess(table, trace.meta[to], replay.cells[to], to_defined) : 0.0;
				const bool pushed = from_defined && from_excess > 0.0;
				const bool pulled = to_defined && to_excess < 0.0;
				const bool unknown = (trace.meta[from].rigid && !from_defined) || (trace.meta[to].rigid && !to_defined);
				if (!pushed && !pulled && !unknown && out.size() < MAX_MESSAGES) {
					out.push_back(text("T1: step %lld: transfer %lld moves gas out of a cell that is not over-full, into one that is not under-full",
							static_cast<long long>(trace.step), static_cast<long long>(i)));
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
			if (whole) {
				// the same probe the step asked, so the same sign
				const AffinityProbe probe(table, replay.cells[from], std::span<const Term>(terms.data(), count), meta_of(trace, from));
				double a = 0.0;
				if (probe.at(0.0, a) && !((entry.extent > 0 && a > 0.0) || (entry.extent < 0 && a < 0.0)) && out.size() < MAX_MESSAGES) {
					char buffer[200];
					std::snprintf(buffer, sizeof buffer, "T1: step %lld: transform %lld runs %lld quanta against an affinity of %.6g J/mol",
							static_cast<long long>(trace.step), static_cast<long long>(i), static_cast<long long>(entry.extent), a);
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
		const CellState &cell = trace.after.cells[c];
		if (std::all_of(cell.amount.begin(), cell.amount.end(), [](int64_t a) { return a == 0; })) {
			// an empty cell (Stage 47: a column's layer while it is mixed) has no temperature, and holds no energy
			if (cell.energy != 0) {
				out.push_back(text("T3: step %lld: empty cell %lld holds %lld quanta of energy", static_cast<long long>(trace.step), static_cast<long long>(c),
						static_cast<long long>(cell.energy)));
			}
			continue;
		}
		const Thermal t = thermal_state(table, cell, meta_of(trace, c));
		if (!t.defined) {
			out.push_back(text("T3: step %lld: cell %lld has no temperature (a species without thermodynamic data, or no heat capacity, or sealed with no room for its gas)",
					static_cast<long long>(trace.step), static_cast<long long>(c)));
		} else if (!(t.temperature > 0.0)) {
			out.push_back(text("T3: step %lld: cell %lld is not above 0 K", static_cast<long long>(trace.step), static_cast<long long>(c)));
		}
	}
	return out;
}

Violations check_X1(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	for (size_t c = 0; c < trace.after.cells.size() && out.size() < MAX_MESSAGES; ++c) {
		const CellState &cell = trace.after.cells[c];
		if (cell.particles.size() != cell.amount.size()) {
			continue;
		}
		for (size_t s = 0; s < cell.amount.size(); ++s) {
			const SpeciesDef &def = table.species(SpeciesId{ static_cast<int32_t>(s) });
			const bool counted = def.phase == Phase::Solid && !def.wall;
			const bool wrong = counted ? (cell.amount[s] > 0) != (cell.particles[s] > 0) || cell.particles[s] < 0 : cell.particles[s] != 0;
			if (wrong && out.size() < MAX_MESSAGES) {
				out.push_back("X1: step " + std::to_string(trace.step) + ": cell " + std::to_string(c) + " holds " + std::to_string(cell.amount[s]) + " quanta of " +
						std::string(def.id) + " in " + std::to_string(cell.particles[s]) + " particles");
			}
		}
	}
	return out;
}

Violations check_X2(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	if (!trace.thermal) {
		return out;
	}
	for (size_t c = 0; c < trace.after.cells.size() && out.size() < MAX_MESSAGES; ++c) {
		if (dry_solute(table, trace.after.cells[c])) {
			out.push_back(text("X2: step %lld: cell %lld holds a dissolved species and no liquid", static_cast<long long>(trace.step), static_cast<long long>(c)));
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
		const Thermo &t = thermo_of(table, trace, trace.after, c);
		if (!t.t_ok) {
			continue; // T3 reports it
		}
		const double kelvin = t.kelvin, s = t.s;
		const bool has_entropy = t.s_ok;
		if (!finite(kelvin) || (has_entropy && !finite(s))) {
			out.push_back(text("S1: step %lld: cell %lld has a temperature or an entropy that is not finite", static_cast<long long>(trace.step), static_cast<long long>(c)));
		}
	}
	return out;
}

Violations check_V1(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	if (!trace.thermal) {
		return out;
	}
	for (size_t c = 0; c < trace.after.cells.size() && out.size() < MAX_MESSAGES; ++c) {
		const CellMeta &meta = trace.meta[c];
		if (!meta.rigid || meta.sealed) {
			continue; // a sealed cell is full by definition: its pressure follows (Stage 28)
		}
		const Thermal thermal = thermal_state(table, trace.after.cells[c], &meta);
		if (!thermal.defined || !(thermal.temperature > 0.0)) {
			continue; // T3 reports it
		}
		const Fill fill = fill_of(table, trace.after.cells[c], thermal.temperature);
		if (!fill.defined) {
			out.push_back(text("V1: step %lld: rigid cell %lld holds a condensed species whose molar volume is unknown", static_cast<long long>(trace.step),
					static_cast<long long>(c)));
			continue;
		}
		const double excess = fill.gas + fill.condensed - meta.volume;
		const double tolerance = static_cast<double>(table.species_count() + 1) * MOL_PER_QUANTUM * GAS_CONSTANT * thermal.temperature / P_ATMOSPHERE + 1e-9 * meta.volume;
		const bool starved = c < trace.starved.size() && trace.starved[c] != 0;
		if (excess > tolerance || (excess < -tolerance && !starved)) {
			char buffer[200];
			std::snprintf(buffer, sizeof buffer, "V1: step %lld: rigid cell %lld of %.6g m^3 is %s by %.6g m^3", static_cast<long long>(trace.step),
					static_cast<long long>(c), meta.volume, excess > 0.0 ? "over-full" : "under-full", excess > 0.0 ? excess : -excess);
			out.push_back(buffer);
		}
	}
	return out;
}

Violations check_V3(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	if (trace.meta.size() != trace.after.cells.size()) {
		return out; // a world without cell kinds has no rigid cells
	}
	for (const Edge &e : trace.edges) {
		if (!cell_in(trace, e.a) || !cell_in(trace, e.b)) {
			return out; // C7's
		}
	}
	std::vector<uint8_t> sealed(trace.meta.size(), 0);
	for (const CellId cell : sealed_cells(trace.meta, trace.edges)) {
		sealed[static_cast<size_t>(cell.index)] = 1;
	}
	for (size_t c = 0; c < trace.meta.size() && out.size() < MAX_MESSAGES; ++c) {
		const CellMeta &meta = trace.meta[c];
		if (meta.sealed != (sealed[c] != 0)) {
			out.push_back(text(meta.sealed ? "V3: step %lld: cell %lld is marked sealed, but it has a gas path to an elastic cell" : "V3: step %lld: cell %lld is marked open, but it has no gas path to an elastic cell",
					static_cast<long long>(trace.step), static_cast<long long>(c)));
			continue;
		}
		if (!meta.sealed) {
			continue;
		}
		bool gas = false;
		for (size_t s = 0; s < trace.after.cells[c].amount.size(); ++s) {
			gas = gas || (trace.after.cells[c].amount[s] > 0 && table.species(SpeciesId{ static_cast<int32_t>(s) }).phase == Phase::Gas);
		}
		// no room for its gas: at 25 C, or (Stage 67, its liquids grown) at any temperature that holds its energy, when
		// open it would have one
		const Fill fill = fill_of(table, trace.after.cells[c], T0);
		const bool roomless = fill.defined && (!(fill.condensed < meta.volume) ||
				(trace.thermal && !thermal_state(table, trace.after.cells[c], &meta).defined && thermal_state(table, trace.after.cells[c]).defined));
		if (gas && roomless) {
			out.push_back(text("V3: step %lld: sealed cell %lld leaves its gas no room: its pressure is not defined", static_cast<long long>(trace.step), static_cast<long long>(c)));
		}
	}
	return out;
}

Violations check_V2(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	if (trace.meta.size() != trace.after.cells.size()) {
		return out;
	}
	for (size_t c = 0; c < trace.after.cells.size() && out.size() < MAX_MESSAGES; ++c) {
		const CellMeta &meta = trace.meta[c];
		if (!meta.rigid) {
			continue;
		}
		const Fill fill = fill_of(table, trace.after.cells[c], volume_kelvin(table, trace, c));
		if (fill.defined && fill.condensed > meta.volume * (1.0 + 1e-12)) {
			char buffer[200];
			std::snprintf(buffer, sizeof buffer, "V2: step %lld: rigid cell %lld of %.6g m^3 holds %.6g m^3 of solids and liquids", static_cast<long long>(trace.step),
					static_cast<long long>(c), meta.volume, fill.condensed);
			out.push_back(buffer);
		}
	}
	return out;
}

Violations check_P1(const SpeciesTable &table, const StepTrace &trace, std::span<const Reaction> equilibria) {
	Violations out;
	if (!trace.thermal) {
		return out;
	}
	const int64_t slack = table.species_count() + 1; // quanta: what moving gas rounds by
	for (size_t c = 0; c < trace.after.cells.size(); ++c) {
		if (trace.meta[c].reservoir || trace.meta[c].frozen) {
			continue; // (Stage 61: a frozen cell's state stands as it was; no process holds it to its equilibria)
		}
		const CellState &cell = trace.after.cells[c];
		for (const Reaction &reaction : equilibria) {
			if (reaction.table() != &table || stands(table, cell, reaction.terms())) {
				continue; // (Stage 63) past the activity model's range, an equilibrium among dissolved species stands
			}
			if (feasible_extent(cell, reaction.terms(), true) <= 0 && feasible_extent(cell, reaction.terms(), false) <= 0) {
				continue; // it can go neither way, whatever its affinity (below): no probe for it (Stage 68)
			}
			const AffinityProbe probe(table, cell, reaction.terms(), meta_of(trace, c));
			double a = 0.0;
			if (!probe.at(0.0, a) || a == 0.0 || a != a) {
				continue;
			}
			const bool forward = a > 0.0;
			const int64_t most = feasible_extent(cell, reaction.terms(), forward);
			if (most <= 0) {
				continue; // nothing left to go that way: dry, or no vapour
			}
			// Gas moved after the equilibrium was taken is rounded to whole
			// quanta, up to `slack` of each species: that shifts the log of a
			// gas's mole fraction by up to slack / n_i (its own amount) plus
			// slack / (the cell's gas), and the affinity by RT times the sum
			// over the reaction's gases of |nu_i| times that. A minor gas (the
			// steam over a cold condenser) moves far more than the whole gas.
			double gas = 0.0;
			for (size_t s = 0; s < cell.amount.size(); ++s) {
				if (cell.amount[s] > 0 && table.species(SpeciesId{ static_cast<int32_t>(s) }).phase == Phase::Gas) {
					gas += static_cast<double>(cell.amount[s]);
				}
			}
			const Thermal thermal = thermal_state(table, cell, meta_of(trace, c));
			double shift = 0.0; // in the log of the activities, summed over the reaction's gases
			for (const Term &t : reaction.terms()) {
				const auto n = static_cast<double>(cell.amount[static_cast<size_t>(t.species.index)]);
				if (table.species(t.species).phase == Phase::Gas && n > 0.0 && gas > 0.0) {
					shift += static_cast<double>(t.nu < 0 ? -t.nu : t.nu) * static_cast<double>(slack) * (1.0 / n + 1.0 / gas);
				}
			}
			const double rounding = shift * GAS_CONSTANT * thermal.temperature;
			if ((forward ? a : -a) <= rounding) {
				continue;
			}
			// otherwise, after the slack or all there is, the direction must be spent
			const int64_t ahead = most < slack ? most : slack;
			double beyond = 0.0;
			const bool has = probe.at(static_cast<double>(forward ? ahead : -ahead), beyond);
			if (has && (forward ? beyond > 0.0 : beyond < 0.0) && out.size() < MAX_MESSAGES) {
				char buffer[240];
				std::snprintf(buffer, sizeof buffer, "P1: step %lld: cell %lld is not at the equilibrium of %s: affinity %.6g J/mol, still %.6g after %lld quanta",
						static_cast<long long>(trace.step), static_cast<long long>(c), std::string(reaction.id()).c_str(), a, beyond, static_cast<long long>(ahead));
				out.push_back(buffer);
			}
		}
	}
	return out;
}

Violations check_L1(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	const size_t cells = trace.after.cells.size();
	std::vector<int32_t> below(cells, -1);
	std::vector<uint8_t> lower(cells, 0);
	for (const Edge &e : trace.edges) {
		if (e.open && e.column && static_cast<size_t>(e.a.index) < cells && static_cast<size_t>(e.b.index) < cells) {
			below[static_cast<size_t>(e.b.index)] = e.a.index;
			lower[static_cast<size_t>(e.a.index)] = 1;
		}
	}
	for (size_t top = 0; top < cells && out.size() < MAX_MESSAGES; ++top) {
		if (below[top] < 0 || lower[top]) {
			continue;
		}
		const CellMeta &m = trace.meta[top];
		double held = 0.0; // m^3 of solids and liquids, the whole column's
		const Fill own = fill_of(table, trace.after.cells[top], volume_kelvin(table, trace, top));
		held += own.defined ? own.condensed : 0.0;
		bool gap = false;
		for (int32_t at = below[top]; at >= 0 && static_cast<size_t>(at) < cells; at = below[static_cast<size_t>(at)]) {
			const CellState &c = trace.after.cells[static_cast<size_t>(at)];
			const bool none = std::all_of(c.amount.begin(), c.amount.end(), [](int64_t a) { return a == 0; });
			if (!none && gap) {
				out.push_back(text("L1: step %lld: layer %lld of column %lld holds something under an empty layer", static_cast<long long>(trace.step), static_cast<long long>(at),
						static_cast<long long>(top)));
				break;
			}
			gap = gap || none;
			const Fill f = fill_of(table, c, volume_kelvin(table, trace, static_cast<size_t>(at)));
			held += f.defined ? f.condensed : 0.0;
		}
		if (m.column_volume > 0.0 && (m.volume > m.column_volume * (1.0 + 1e-12) || held > m.column_volume * (1.0 + 1e-9))) {
			char buffer[240];
			std::snprintf(buffer, sizeof buffer, "L1: step %lld: column %lld of %.6g m^3 has a top of %.6g m^3 and holds %.6g m^3 of solids and liquids", static_cast<long long>(trace.step),
					static_cast<long long>(top), m.column_volume, m.volume, held);
			out.push_back(buffer);
		}
	}
	return out;
}

Violations check_E1(const SpeciesTable &table, const StepTrace &trace) {
	Violations out;
	for (const Exchanged &x : trace.exchanged) {
		if (out.size() >= MAX_MESSAGES) {
			break;
		}
		const bool known = x.edge.index >= 0 && static_cast<size_t>(x.edge.index) < trace.edges.size();
		const Edge *e = known ? &trace.edges[static_cast<size_t>(x.edge.index)] : nullptr;
		const bool joins = e != nullptr && e->open && e->exchange > 0.0 && (e->phases & PHASES_GAS) != 0 &&
				((e->a == x.from && e->b == x.to) || (e->a == x.to && e->b == x.from));
		const bool gas = x.species.index >= 0 && x.species.index < static_cast<int32_t>(table.species_count()) && table.species(x.species).phase == Phase::Gas;
		// at most what takes the two fractions to equal, and a quantum besides (the move is a whole number of them)
		const double most = x.gas_from > 0.0 && x.gas_to > 0.0 ? (x.x_from - x.x_to) / (1.0 / x.gas_from + 1.0 / x.gas_to) : 0.0;
		if (!joins || !gas) {
			out.push_back(text("E1: step %lld: a gas exchanged along edge %lld, which does not exchange gas between those cells", static_cast<long long>(trace.step),
					static_cast<long long>(x.edge.index)));
		} else if (x.moved <= 0 || x.x_from < x.x_to) {
			out.push_back(text("E1: step %lld: species %lld moved against its mole fraction, or nothing", static_cast<long long>(trace.step), static_cast<long long>(x.species.index)));
		} else if (static_cast<double>(x.moved - 1) * MOL_PER_QUANTUM > most * (1.0 + 1e-12)) {
			out.push_back(text("E1: step %lld: species %lld moved %lld quanta, past equal fractions", static_cast<long long>(trace.step), static_cast<long long>(x.species.index),
					static_cast<long long>(x.moved)));
		}
	}
	return out;
}

// (Stage 79) a message of any arguments
template <typename... Args>
std::string words(const char *format, Args... args) {
	char buffer[256];
	std::snprintf(buffer, sizeof buffer, format, args...);
	return buffer;
}

Violations check_W1(const SpeciesTable &, const StepTrace &trace) {
	Violations out;
	if (trace.meta.size() != trace.after.cells.size()) {
		return out;
	}
	for (size_t c = 0; c < trace.meta.size() && out.size() < MAX_MESSAGES; ++c) {
		const Piston &p = trace.meta[c].piston;
		if (!(p.area > 0.0)) {
			continue;
		}
		const double volume = trace.meta[c].column_volume > 0.0 ? trace.meta[c].column_volume : trace.meta[c].volume;
		if (volume < p.least * (1.0 - 1e-12) || volume > p.most * (1.0 + 1e-12)) {
			out.push_back(words("W1: step %lld: cell %lld's piston stands at %.9g m^3, past its stops (%.9g to %.9g)", static_cast<long long>(trace.step),
					static_cast<long long>(c), volume, p.least, p.most));
		}
		if (std::fabs(p.force) > p.hand_most * (1.0 + 1e-12) || (!p.held && p.force != 0.0)) {
			out.push_back(words("W1: step %lld: cell %lld's hand pushes with %.9g N, more than it can (%.9g N%s)", static_cast<long long>(trace.step),
					static_cast<long long>(c), p.force, p.hand_most, p.held ? "" : ", let go"));
		}
		int64_t along = 0;
		for (const LedgerEntry &e : trace.ledger) {
			if (e.kind == LedgerEntry::Kind::Transfer && e.edge == p.work) {
				along += e.to.index == static_cast<int32_t>(c) ? e.energy.quanta : -e.energy.quanta;
			}
		}
		const double work = p.force * p.moved / p.area / JOULE_PER_QUANTUM;
		if (std::fabs(static_cast<double>(along) - work) > 1.0) {
			out.push_back(words("W1: step %lld: cell %lld's piston took %lld quanta along its work edge, the hand's work %.6g", static_cast<long long>(trace.step),
					static_cast<long long>(c), static_cast<long long>(along), work));
		}
	}
	return out;
}

Violations check_all(const SpeciesTable &table, const StepTrace &trace, std::span<const Reaction> equilibria) {
	if (!well_formed(table, trace)) {
		return Violations{ text("SHAPE: step %lld: the trace's cell or species counts disagree", static_cast<long long>(trace.step)) };
	}
	Violations out = check_P1(table, trace, equilibria);
	{
		Violations v = check_V2(table, trace);
		out.insert(out.end(), v.begin(), v.end());
	}
	{
		const Totals a = totals(table, trace.before), b = totals(table, trace.after);
		for (Violations v : { c1_of(table, trace, a, b), c2_of(table, trace, a, b), c3_of(table, trace, a, b), c4_of(table, trace, a, b) }) {
			out.insert(out.end(), v.begin(), v.end());
		}
	}
	for (const auto check : { check_C5, check_C6, check_C7, check_C8, check_T1, check_T2, check_T3, check_X1, check_X2, check_S1, check_V1, check_V3, check_L1, check_E1, check_N1, check_W1 }) {
		Violations v = check(table, trace);
		out.insert(out.end(), v.begin(), v.end());
	}
	return out;
}

} // namespace chem
