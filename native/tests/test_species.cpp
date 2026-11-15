// Species tables and reactions: an unbalanced reaction cannot exist (GAME_SPEC §4.1, §4.3).
#include "chem/builtin_ids.hpp"
#include "chem/species.hpp"

#include <doctest/doctest.h>

using namespace chem;

namespace {

SpeciesTable small_table() {
	// elements X, Y; species x(s), x2(g), xy(l), x+(aq), y(s)
	std::string why;
	auto table = SpeciesTable::make({ { "X", 1000000 }, { "Y", 3000000 } },
			{
					{ "x", "X", Phase::Solid, 0, Atoms{ { 1, 0 } } },
					{ "x2", "X2", Phase::Gas, 0, Atoms{ { 2, 0 } } },
					{ "xy", "XY", Phase::Liquid, 0, Atoms{ { 1, 1 } } },
					{ "x_plus", "X", Phase::Aqueous, 1, Atoms{ { 1, 0 } } },
					{ "y", "Y", Phase::Solid, 0, Atoms{ { 0, 1 } } },
			},
			&why);
	REQUIRE_MESSAGE(table.has_value(), why);
	return *table;
}

} // namespace

TEST_CASE("species: molar masses are derived from the element counts (Stage 1)") {
	const SpeciesTable table = small_table();
	CHECK(table.molar_mass_ug(table.find("x")) == 1000000);
	CHECK(table.molar_mass_ug(table.find("x2")) == 2000000);
	CHECK(table.molar_mass_ug(table.find("xy")) == 4000000);
	CHECK_FALSE(table.has(table.find("nothing")));
}

TEST_CASE("species: a malformed table is refused with a reason (Stage 1)") {
	std::string why;
	CHECK_FALSE(SpeciesTable::make({}, {}, &why));
	CHECK_FALSE(SpeciesTable::make({ { "X", 0 } }, {}, &why));
	CHECK_FALSE(SpeciesTable::make({ { "X", 1 }, { "X", 2 } }, {}, &why));
	CHECK_FALSE(SpeciesTable::make({ { "X", 1 } }, { { "a", "X", Phase::Solid, 0, Atoms{ { -1 } } } }, &why));
	CHECK_FALSE(SpeciesTable::make({ { "X", 1 } }, { { "a", "?", Phase::Solid, 0, Atoms{} } }, &why)); // no atoms
	CHECK_FALSE(SpeciesTable::make({ { "X", 1 } }, { { "a", "XY", Phase::Solid, 0, Atoms{ { 1, 1 } } } }, &why)); // an element it does not have
	CHECK_FALSE(SpeciesTable::make({ { "X", 1 } }, { { "a", "X", Phase::Solid, 0, Atoms{ { 1 } } }, { "a", "X2", Phase::Solid, 0, Atoms{ { 2 } } } }, &why));
	CHECK_FALSE(SpeciesTable::make({ { "X", int64_t{ 1 } << 30 } }, { { "a", "X", Phase::Solid, 0, Atoms{ { 1 } } } }, &why)); // too heavy
	CHECK_FALSE(why.empty());
}

TEST_CASE("species: a balanced reaction is made; an unbalanced one cannot be (Stage 1)") {
	const SpeciesTable table = small_table();
	const SpeciesId x = table.find("x"), x2 = table.find("x2"), xy = table.find("xy"), x_plus = table.find("x_plus"), y = table.find("y");
	std::string why;

	const Term dimerise[] = { { x, -2 }, { x2, 1 } };
	CHECK(Reaction::make(table, "dimerise", dimerise, &why).has_value());
	const Term combine[] = { { x, -1 }, { y, -1 }, { xy, 1 } };
	CHECK(Reaction::make(table, "combine", combine, &why).has_value());

	const Term lost_atom[] = { { x, -1 }, { x2, 1 } }; // X is created
	CHECK_FALSE(Reaction::make(table, "lost_atom", lost_atom, &why));
	CHECK(why.find("balance") != std::string::npos);
	const Term lost_charge[] = { { x, -1 }, { x_plus, 1 } }; // the atoms balance, the charge does not
	CHECK_FALSE(Reaction::make(table, "lost_charge", lost_charge, &why));
	CHECK(why.find("balance") != std::string::npos);
	const Term transmute[] = { { x, -1 }, { y, 1 } };
	CHECK_FALSE(Reaction::make(table, "transmute", transmute, &why));

	const Term zero[] = { { x, -2 }, { x2, 1 }, { y, 0 } };
	CHECK_FALSE(Reaction::make(table, "zero", zero, &why));
	const Term twice[] = { { x, -1 }, { x, -1 }, { x2, 1 } };
	CHECK_FALSE(Reaction::make(table, "twice", twice, &why));
	const Term unknown[] = { { SpeciesId{ 99 }, -2 }, { x2, 1 } };
	CHECK_FALSE(Reaction::make(table, "unknown", unknown, &why));
	CHECK_FALSE(Reaction::make(table, "empty", {}, &why));
}

TEST_CASE("species: balanced() is a compile-time proof (Stage 1)") {
	static constexpr SpeciesDef SPECIES[] = {
		{ "x", "X", Phase::Solid, 0, Atoms{ { 1 } } },
		{ "x2", "X2", Phase::Gas, 0, Atoms{ { 2 } } },
	};
	static constexpr Term GOOD[] = { { SpeciesId{ 0 }, -2 }, { SpeciesId{ 1 }, 1 } };
	static constexpr Term BAD[] = { { SpeciesId{ 0 }, -1 }, { SpeciesId{ 1 }, 1 } };
	static_assert(balanced(SPECIES, GOOD));
	static_assert(!balanced(SPECIES, BAD));
	CHECK(balanced(SPECIES, GOOD));
}

TEST_CASE("species: the built-in tables load, every reaction balanced (Stage 1)") {
	const SpeciesTable &table = builtin_species();
	CHECK(table.species_count() == builtin::SPECIES_COUNT);
	CHECK(builtin_reactions().size() == static_cast<size_t>(builtin::reaction::COUNT));
	// Cu2CO3(OH)2: 2 Cu + C + 5 O + 2 H = 221.114 g/mol
	CHECK(table.molar_mass_ug(builtin::malachite) == 221114000);
	CHECK(table.molar_mass_ug(builtin::copper_sulfate_pentahydrate) == 249677000);
	CHECK(table.molar_mass_ug(builtin::water) == table.molar_mass_ug(builtin::steam));
	CHECK(table.species(builtin::steam).phase == Phase::Gas);
	CHECK(table.find("malachite") == builtin::malachite);
	for (const Reaction &r : builtin_reactions()) {
		CAPTURE(r.id());
		CHECK(balanced(table.all_species(), r.terms()));
	}
	CHECK(builtin_reactions()[static_cast<size_t>(builtin::reaction::malachite_decomposition)].id() == "malachite_decomposition");
}
