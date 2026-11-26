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

namespace {

// the binary's own path, made canonical ("" when it cannot be read)
std::filesystem::path binary_path() {
	std::string exe;
#if defined(__APPLE__)
	uint32_t size = 0;
	_NSGetExecutablePath(nullptr, &size);
	exe.resize(size);
	if (_NSGetExecutablePath(exe.data(), &size) != 0) {
		return {};
	}
	exe.resize(std::strlen(exe.c_str()));
#else
	std::error_code error;
	exe = std::filesystem::read_symlink("/proc/self/exe", error).string();
	if (error) {
		return {};
	}
#endif
	std::error_code error_canonical;
	return std::filesystem::weakly_canonical(exe, error_canonical);
}

} // namespace

const char *chem_repo_root() {
	static const std::string root = [] {
		if (const char *v = std::getenv("CHEM_REPO_ROOT"); v != nullptr && *v != '\0') {
			return std::string(v);
		}
		const std::filesystem::path binary = binary_path();
		// <root>/native/<tree>/<binary>
		return binary.empty() ? std::string(".") : binary.parent_path().parent_path().parent_path().string();
	}();
	return root.c_str();
}

const char *chem_binary_dir() {
	static const std::string dir = [] {
		const std::filesystem::path binary = binary_path();
		return binary.empty() ? std::string(".") : binary.parent_path().string();
	}();
	return dir.c_str();
}
