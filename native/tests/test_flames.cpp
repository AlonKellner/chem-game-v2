// Flame emission (GAME_SPEC §12, Stage 82): lithium and strontium, whose salts colour a flame; the lines each
// element emits in a flame at its temperature, against NIST's. docs/DATA_SOURCES.md, "Lithium and strontium (Stage
// 82)" and "Flame emission (Stage 82)".
#include "tables.hpp"
#include "chem/builtin_ids.hpp"
#include "chem/thermo.hpp"

#include <doctest/doctest.h>

#include <cmath>

using namespace chem;

TEST_CASE("measured: lithium's and strontium's ions are NBS's and OBIGT's, their salts' pairs Pitzer and Mayorga's (Stage 82)") {
	using namespace builtin;
	const SpeciesDef &li = builtin_species().species(lithium_ion), &sr = builtin_species().species(strontium_ion);
	// NBS 1982 (Li+ p. 2-290, Sr2+ p. 2-276), J/mol and J/mol K
	CHECK(li.hf == doctest::Approx(-278490.0));
	CHECK(li.s0 == doctest::Approx(13.4));
	CHECK(sr.hf == doctest::Approx(-545800.0));
	CHECK(sr.s0 == doctest::Approx(-32.6));
	// OBIGT on the model's footing: Cp - 216.35 z, V - 16.74 z
	CHECK(li.cp == doctest::Approx(14.2 * 4.184 - 216.35).epsilon(1e-5));
	CHECK(sr.cp == doctest::Approx(-10.05 * 4.184 - 2.0 * 216.35).epsilon(1e-5));
	CHECK(li.molar_volume * 1e6 == doctest::Approx(-0.87 - 16.74).epsilon(1e-5));
	CHECK(sr.molar_volume * 1e6 == doctest::Approx(-17.41 - 2.0 * 16.74).epsilon(1e-5));
	// Pitzer and Mayorga 1973's pairs as the model reads them; and the CRC Handbook's diffusion (Vanysek)
	const SolutionDef &solution = builtin_species().solution();
	bool li_cl = false, sr_cl = false;
	for (const PairDef &p : solution.pairs) {
		const std::string_view c = solution.ions[static_cast<size_t>(p.cation)].name, a = solution.ions[static_cast<size_t>(p.anion)].name;
		if (c == "Li+1" && a == "Cl-1") {
			li_cl = true;
			CHECK(p.beta0 == doctest::Approx(0.1494));
			CHECK(p.beta1 == doctest::Approx(0.3074));
			CHECK(p.c_phi == doctest::Approx(0.00359));
		}
		if (c == "Sr+2" && a == "Cl-1") {
			sr_cl = true;
			CHECK(p.beta0 == doctest::Approx(0.28575));
			CHECK(p.beta1 == doctest::Approx(1.66725));
		}
	}
	CHECK(li_cl);
	CHECK(sr_cl);
	for (const IonDef &ion : solution.ions) {
		if (ion.name == "Li+1") {
			CHECK(ion.diffusion == doctest::Approx(1.029e-9));
		}
		if (ion.name == "Sr+2") {
			CHECK(ion.diffusion == doctest::Approx(0.791e-9));
		}
	}
}
