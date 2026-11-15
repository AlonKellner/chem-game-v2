// Compiles CHEM_ASSERT in its release (non-aborting) form regardless of the build
// configuration, so the sink path is covered by Debug and ASan runs as well.
#undef CHEM_ASSERT_ABORTS
#define CHEM_ASSERT_ABORTS 0
#include "chem/error.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

TEST_CASE("error: release-mode CHEM_ASSERT reports through the sink and continues (Stage 0)") {
	std::vector<std::string> got;
	chem::set_error_sink([&got](const char *message) { got.emplace_back(message); });

	CHEM_ASSERT(1 + 1 == 2, "never fires");
	CHECK(got.empty());

	CHEM_ASSERT(1 + 1 == 3, "arithmetic broke");
	REQUIRE(got.size() == 1);
	CHECK(got[0].find("test_error_sink.cpp") != std::string::npos);
	CHECK(got[0].find("1 + 1 == 3") != std::string::npos);
	CHECK(got[0].find("arithmetic broke") != std::string::npos);

	chem::set_error_sink(nullptr);
}

TEST_CASE("error: report_error uses the installed sink; an empty sink is allowed (Stage 0)") {
	std::string last;
	chem::set_error_sink([&last](const char *message) { last = message; });
	chem::report_error("hello sink");
	CHECK(last == "hello sink");

	chem::set_error_sink(nullptr);
	chem::report_error("(expected on stderr) the default sink does not crash");
}
