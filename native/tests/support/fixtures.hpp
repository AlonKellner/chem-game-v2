#pragma once
// A ctest fixture's files (Stage 68; native/CMakeLists.txt, `// ctest: fixture setup NAME` and `// ctest: fixture
// NAME`): what one case makes once for the cases that start from it, in LAB_FIXTURES/NAME, by default fixtures/NAME
// in the tree ctest runs the entries in. ctest runs the setup first whenever a case that needs it runs.
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fixtures {

inline std::string dir(const std::string &name) {
	const char *v = std::getenv("LAB_FIXTURES");
	return (v != nullptr && *v != '\0' ? std::string(v) : std::string("fixtures")) + "/" + name;
}

// a fresh directory for the setup to write
inline std::string fresh(const std::string &name) {
	const std::string d = dir(name);
	std::filesystem::remove_all(d);
	std::filesystem::create_directories(d);
	return d;
}

inline void write(const std::string &name, const std::string &file, const std::string &text) {
	std::ofstream out(dir(name) + "/" + file, std::ios::binary);
	REQUIRE_MESSAGE(static_cast<bool>(out), dir(name) << "/" << file);
	out << text;
}

inline std::string read(const std::string &name, const std::string &file) {
	std::ifstream in(dir(name) + "/" + file, std::ios::binary);
	REQUIRE_MESSAGE(static_cast<bool>(in), "no fixture " << dir(name) << "/" << file << " (the fixture's setup makes it: ctest runs it first)");
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

} // namespace fixtures
