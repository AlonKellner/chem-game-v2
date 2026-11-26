// chem_tool run / bench: a session script (data/sessions/*.json) played through
// the C API alone, as any host plays the game (GAME_SPEC §13, §14).
#include "chem_api.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using json = nlohmann::json;

bool read(const std::filesystem::path &path, std::string &text) {
	std::ifstream in(path);
	if (!in) {
		return false;
	}
	std::ostringstream out;
	out << in.rdbuf();
	text = out.str();
	return true;
}

// The measured results of the demo: what was submitted, and the conservation laws.
void print_results(const json &report, const json &start) {
	std::printf("\nmeasured after %.0f s of simulated time (%lld steps)\n", report.at("seconds").get<double>(), static_cast<long long>(report.at("step").get<int64_t>()));
	bool warned = false; // what the lab warned of, and its dangers (GAME_SPEC §13.4)
	for (const json &n : report.value("notices", json::array())) {
		const std::string level = n.value("level", "");
		if (level == "warning" || level == "danger") {
			std::printf("%s  %8.1f s  %-7s %s\n", warned ? "" : "\n  warnings and dangers:\n", static_cast<double>(n.at("step").get<int64_t>()) / 60.0, level.c_str(),
					n.at("says").get<std::string>().c_str());
			warned = true;
		}
	}
	for (const json &item : report.at("items")) {
		if (item.at("kind") == "burner" || !item.at("submitted").get<bool>()) {
			continue;
		}
		std::printf("\n  submitted: %s, at %.2f C, %.1f mL of gas, %.2f mL of solids and liquids\n", item.at("id").get<std::string>().c_str(), item.at("celsius").get<double>(),
				item.at("gas_ml").get<double>(), item.at("condensed_ml").get<double>());
		double gas_mol = 0.0, condensed_g = 0.0;
		for (const json &row : item.at("contents")) {
			if (row.at("wall").get<bool>()) {
				continue;
			}
			if (row.at("phase") == "g") {
				gas_mol += row.at("mol").get<double>();
			} else {
				condensed_g += row.at("g").get<double>();
			}
		}
		for (const json &row : item.at("contents")) {
			if (row.at("wall").get<bool>()) {
				continue;
			}
			const std::string name = row.value("written", row.at("formula").get<std::string>()) + "(" + row.at("phase").get<std::string>() + ")";
			if (row.at("phase") == "g") {
				std::printf("    %-16s %10.6f g  %6.2f%% of the gas\n", name.c_str(), row.at("g").get<double>(), 100.0 * row.at("mol").get<double>() / gas_mol);
			} else if (row.contains("particles")) {
				std::printf("    %-16s %10.6f g  %7.3f%% of the solids and liquids, in %lld particles of %.1f um\n", name.c_str(), row.at("g").get<double>(),
						100.0 * row.at("g").get<double>() / condensed_g, static_cast<long long>(row.at("particles").get<int64_t>()), row.at("size_um").get<double>());
			} else {
				std::printf("    %-16s %10.6f g  %7.3f%% of the solids and liquids\n", name.c_str(), row.at("g").get<double>(), 100.0 * row.at("g").get<double>() / condensed_g);
			}
		}
	}
	const json &now = report.at("conserved");
	// (Stage 61) the start with what came in from Supplies and what left from the trash (C9): what the end should be
	const json &then = now.contains("expected") ? now.at("expected") : start.at("conserved");
	if (now.value("supplied_g", 0.0) != 0.0 || now.value("retired_g", 0.0) != 0.0) {
		std::printf("\n  across the world's boundary: %.9f g supplied, %.9f g retired\n", now.at("supplied_g").get<double>(), now.at("retired_g").get<double>());
	}
	std::printf("\n  conservation, start (with what crossed its boundary) -> end (integers):\n");
	for (const auto &[symbol, atoms] : now.at("atom_quanta").items()) {
		std::printf("    %-3s %22lld -> %22lld quanta of atoms\n", symbol.c_str(), static_cast<long long>(then.at("atom_quanta").at(symbol).get<int64_t>()),
				static_cast<long long>(atoms.get<int64_t>()));
	}
	std::printf("    energy %19lld -> %22lld quanta (1e-9 J)\n", static_cast<long long>(then.at("energy_quanta").get<int64_t>()),
			static_cast<long long>(now.at("energy_quanta").get<int64_t>()));
	std::printf("    mass   %.9f g -> %.9f g\n", then.at("mass_g").get<double>(), now.at("mass_g").get<double>());
	std::printf("    %s\n", now.at("unchanged").get<bool>() ? "every element, the mass, the charge and the energy: unchanged" : "THE TOTALS MOVED");
}

} // namespace

// run:   plays the session with every invariant checked on every step.
//          --record FILE   writes one line per command: its index, the step it ended on, the state's digest
//          --check FILE    compares with a recorded file instead
//          --quiet         no per-command lines
// bench: plays it with the checks off and times it.
int run_session(int argc, char **argv, bool bench) {
	std::string session_path, record_path, check_path;
	bool quiet = false;
	for (int i = 0; i < argc; ++i) {
		if (std::strcmp(argv[i], "--record") == 0 && i + 1 < argc) {
			record_path = argv[++i];
		} else if (std::strcmp(argv[i], "--check") == 0 && i + 1 < argc) {
			check_path = argv[++i];
		} else if (std::strcmp(argv[i], "--quiet") == 0) {
			quiet = true;
		} else {
			session_path = argv[i];
		}
	}
	std::string text;
	if (session_path.empty() || !read(session_path, text)) {
		std::fprintf(stderr, "chem_tool: cannot read the session \"%s\"\n", session_path.c_str());
		return 2;
	}
	json session;
	try {
		session = json::parse(text, nullptr, true, true);
	} catch (const std::exception &e) {
		std::fprintf(stderr, "chem_tool: %s is not JSON: %s\n", session_path.c_str(), e.what());
		return 2;
	}
	if (session.value("format", "") != "chem-session/1" || !session.contains("script")) {
		std::fprintf(stderr, "chem_tool: %s is not a \"chem-session/1\"\n", session_path.c_str());
		return 2;
	}
	std::string bench_text;
	const std::filesystem::path bench_path = std::filesystem::path(session_path).parent_path() / session.value("bench", "");
	if (!read(bench_path, bench_text)) {
		std::fprintf(stderr, "chem_tool: cannot read the bench \"%s\"\n", bench_path.string().c_str());
		return 2;
	}
	cg_lab *lab = cg_lab_open(bench_text.c_str());
	if (lab == nullptr) {
		std::fprintf(stderr, "chem_tool: %s\n", cg_last_error());
		return 1;
	}
	cg_lab_set_checking(lab, bench ? 0 : 1);
	const json start = json::parse(cg_lab_report(lab));

	std::vector<std::string> lines;
	int status = 0;
	const auto t0 = std::chrono::steady_clock::now();
	int index = 0;
	for (const json &command : session.at("script")) {
		if (command.contains("note") && !quiet && !bench) {
			std::printf("\n%s\n", command.at("note").get<std::string>().c_str());
		}
		json plain = command;
		plain.erase("note");
		const std::string spelled = plain.dump();
		const cg_status result = cg_lab_command(lab, spelled.c_str());
		if (result != CG_OK) {
			std::fprintf(stderr, "chem_tool: command %d %s: %s\n", index, spelled.c_str(), cg_last_error());
			status = 1;
			break;
		}
		json line;
		line["i"] = index;
		line["do"] = command.at("do");
		line["step"] = cg_lab_step(lab);
		line["digest"] = cg_lab_digest(lab);
		lines.push_back(line.dump());
		if (!quiet && !bench) {
			std::printf("  %2d  %8.1f s  %s\n", index, static_cast<double>(cg_lab_step(lab)) / 60.0, spelled.c_str());
		}
		++index;
	}
	const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	const json report = json::parse(cg_lab_report(lab));
	const int64_t steps = cg_lab_step(lab);
	cg_lab_close(lab);

	if (bench) {
		std::printf("%lld steps (%.0f s of simulated time) in %.3f s: %.2f us a step, %.0f times real time\n", static_cast<long long>(steps), static_cast<double>(steps) / 60.0, wall,
				wall / static_cast<double>(steps) * 1e6, static_cast<double>(steps) / 60.0 / wall);
		std::printf("at 600 steps a frame (ten simulated seconds a frame): %.2f ms a frame\n", wall / static_cast<double>(steps) * 600.0 * 1e3);
		return status;
	}
	if (status == 0) {
		print_results(report, start);
		if (!report.at("conserved").at("unchanged").get<bool>()) {
			status = 1;
		}
	}
	if (!record_path.empty() && status == 0) {
		std::ofstream out(record_path);
		for (const std::string &line : lines) {
			out << line << "\n";
		}
		std::printf("\nrecorded %zu commands to %s\n", lines.size(), record_path.c_str());
	}
	if (!check_path.empty() && status == 0) {
		std::ifstream in(check_path);
		std::string expected;
		size_t at = 0;
		bool same = static_cast<bool>(in);
		while (same && std::getline(in, expected)) {
			same = at < lines.size() && lines[at] == expected;
			if (!same) {
				std::fprintf(stderr, "chem_tool: the session differs from %s at command %zu\n  recorded: %s\n  now:      %s\n", check_path.c_str(), at, expected.c_str(),
						at < lines.size() ? lines[at].c_str() : "(nothing)");
			}
			++at;
		}
		if (same && at != lines.size()) {
			std::fprintf(stderr, "chem_tool: the session has %zu commands, %s has %zu\n", lines.size(), check_path.c_str(), at);
			same = false;
		}
		std::printf("\n%s\n", same ? "the session reproduces its record, digest for digest" : "THE SESSION DIFFERS FROM ITS RECORD");
		status = same ? 0 : 1;
	}
	return status;
}
