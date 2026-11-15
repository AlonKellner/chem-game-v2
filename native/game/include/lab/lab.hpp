#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

// The engine-free lab layer (GAME_SPEC §13): a bench of equipment over one
// closed world, driven by commands. Everything a player can do is a command;
// a session is a list of them. The lab does no file access: a host hands it
// the bench as text.
namespace lab {

// "chem-game-v2 <GAME_SPEC version>"
std::string build_name();

class Lab {
public:
	// A bench (data/benches/*.json, "chem-bench/1"): the room, and the
	// equipment with what it holds. null, with the reason in `why`, when the
	// text is not a bench or describes a world that cannot be built.
	static std::unique_ptr<Lab> open(std::string_view bench_json, std::string &why);
	~Lab();

	// One command, a JSON object with a "do" (GAME_SPEC §13 lists them).
	// false with the reason in `why` when it is refused: the state is then as
	// it was, except that a command which changes connections first takes the
	// lab to a step boundary (at most one step). Also false when an invariant
	// failed on the way: failed() is then true and the lab takes no more.
	bool command(std::string_view command_json, std::string &why);

	// Advances whole steps of 1/60 s.
	bool advance(int64_t steps, std::string &why);

	// The whole lab as text ("chem-save/1"): every amount, energy and particle
	// count, the connections, the processes' carried remainders, where
	// everything stands. load() gives a lab that continues exactly as this one
	// would have: the same digests, step for step (GAME_SPEC §18: D2).
	std::string save() const;
	static std::unique_ptr<Lab> load(std::string_view save_json, std::string &why);

	// Every invariant on every step (GAME_SPEC §18). On by default.
	void set_checking(bool on);
	bool failed() const;

	int64_t step() const;
	// What is where, measured (JSON; GAME_SPEC §13: reports are functions of state).
	std::string report() const;
	// SHA-256 of the whole state: amounts, energies, particles, the processes'
	// carried remainders, the connections, the step. Equal digests are equal states.
	std::string digest() const;

private:
	struct Impl;
	explicit Lab(std::unique_ptr<Impl> impl);
	std::unique_ptr<Impl> impl_;
};

} // namespace lab
