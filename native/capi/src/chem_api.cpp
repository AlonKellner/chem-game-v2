#include "chem_api.h"

#include "chem/error.hpp"
#include "lab/lab.hpp"

#include <string>

namespace {

std::string &last_error() {
	thread_local std::string message;
	return message;
}

cg_status fail(cg_status status, const char *message) {
	last_error() = message;
	return status;
}

} // namespace

extern "C" {

int32_t cg_api_version(void) {
	return CG_API_VERSION;
}

const char *cg_build_name(void) {
	thread_local std::string name;
	name = lab::build_name();
	return name.c_str();
}

const char *cg_last_error(void) {
	return last_error().c_str();
}

void cg_set_log(cg_log_fn fn, void *user) {
	if (fn == nullptr) {
		chem::set_error_sink(nullptr);
		return;
	}
	chem::set_error_sink([fn, user](const char *message) { fn(user, message); });
}

cg_status cg_debug_report(const char *message) {
	if (message == nullptr) {
		return fail(CG_ERR_INVALID_ARGUMENT, "cg_debug_report: null message");
	}
	chem::report_error(message);
	return fail(CG_ERR_INTERNAL, message);
}

} // extern "C"
