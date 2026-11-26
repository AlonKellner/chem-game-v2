// The repository's root at run time (Stage 68; repo_root.hpp).
#include "repo_root.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

const char *chem_repo_root() {
	static const std::string root = [] {
		if (const char *v = std::getenv("CHEM_REPO_ROOT"); v != nullptr && *v != '\0') {
			return std::string(v);
		}
		std::string exe;
#if defined(__APPLE__)
		uint32_t size = 0;
		_NSGetExecutablePath(nullptr, &size);
		exe.resize(size);
		if (_NSGetExecutablePath(exe.data(), &size) != 0) {
			return std::string(".");
		}
		exe.resize(std::strlen(exe.c_str()));
#else
		std::error_code error;
		exe = std::filesystem::read_symlink("/proc/self/exe", error).string();
		if (error) {
			return std::string(".");
		}
#endif
		std::error_code error_canonical;
		const std::filesystem::path binary = std::filesystem::weakly_canonical(exe, error_canonical);
		// <root>/native/<tree>/<binary>
		return binary.parent_path().parent_path().parent_path().string();
	}();
	return root.c_str();
}
