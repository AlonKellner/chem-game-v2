#include "lab/lab.hpp"

#include "chem/version.hpp"

namespace lab {

std::string build_name() {
	return std::string("chem-game-v2 ") + chem::SPEC_VERSION;
}

} // namespace lab
