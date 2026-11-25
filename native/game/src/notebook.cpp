// The notebook (GAME_SPEC §13, Stage 54).
#include "lab/notebook.hpp"
#include "lab/conditions.hpp"

#include <cctype>
#include <cmath>

namespace lab {

namespace {

const ItemView *find_item(const LabView &view, const std::string &id) {
	for (const ItemView &v : view.items) {
		if (v.id == id) {
			return &v;
		}
	}
	return nullptr;
}

} // namespace

std::optional<double> reading(const LabView &view, const Quantity &q, bool &instrument) {
	instrument = false;
	if (!view.instruments) {
		return measure(view, q);
	}
	const ItemView *v = find_item(view, q.item);
	if (v == nullptr) {
		return std::nullopt;
	}
	instrument = true;
	if (q.what == "celsius" && v->kind == "vessel") {
		return v->read_c;
	}
	if (q.what == "ph") {
		return v->read_ph;
	}
	if (q.what == "liquid_ml" || q.what == "condensed_ml") {
		return v->scale_ml ? std::nullopt : v->read_ml; // (a burette reads its scale, not its liquid)
	}
	if (q.what == "scale_ml") {
		return v->read_ml;
	}
	if (q.what == "reads_g") {
		return measure(view, q); // a balance's display is an instrument's already
	}
	if (q.what == "kpa" && v->kind == "vessel") {
		return v->read_kpa;
	}
	if (q.what == "tap" || q.what == "valve" || q.what == "met" || q.what == "mark_off_ml") {
		instrument = false;
		return measure(view, q); // what is set, or seen at a glance: not an instrument's reading
	}
	return std::nullopt; // what no instrument reads
}

LineFit fit_line(const std::vector<double> &x, const std::vector<double> &y) {
	LineFit f;
	const size_t n = std::min(x.size(), y.size());
	f.n = static_cast<int>(n);
	if (n < 2) {
		return f;
	}
	double sx = 0.0, sy = 0.0;
	for (size_t i = 0; i < n; ++i) {
		sx += x[i];
		sy += y[i];
	}
	const double mx = sx / static_cast<double>(n), my = sy / static_cast<double>(n);
	double sxx = 0.0, sxy = 0.0, syy = 0.0;
	for (size_t i = 0; i < n; ++i) {
		sxx += (x[i] - mx) * (x[i] - mx);
		sxy += (x[i] - mx) * (y[i] - my);
		syy += (y[i] - my) * (y[i] - my);
	}
	if (!(sxx > 0.0)) {
		return f;
	}
	f.ok = true;
	f.slope = sxy / sxx;
	f.intercept = my - f.slope * mx;
	f.r2 = syy > 0.0 ? sxy * sxy / (sxx * syy) : 1.0;
	return f;
}

std::optional<double> transformed(double value, AxisTransform t, bool celsius) {
	switch (t) {
	case AxisTransform::Plain:
		return value;
	case AxisTransform::Log:
		return value > 0.0 ? std::optional<double>(std::log(value)) : std::nullopt;
	case AxisTransform::Inverse: {
		const double v = celsius ? value + 273.15 : value;
		return v != 0.0 ? std::optional<double>(1.0 / v) : std::nullopt;
	}
	}
	return std::nullopt;
}

const char *transform_name(AxisTransform t, bool celsius) {
	return t == AxisTransform::Log ? "ln " : t == AxisTransform::Inverse ? (celsius ? "1/K of " : "1/") : "";
}

namespace {

// a recursive descent over: expression = term {(+|-) term}; term = factor {(*|/) factor}; factor = number | name |
// ( expression ) | - factor
struct Parser {
	std::string_view s;
	size_t at = 0;
	const LabView &view;
	std::string why;
	void space() {
		while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at]))) {
			++at;
		}
	}
	bool expression(double &v) {
		if (!term(v)) {
			return false;
		}
		for (;;) {
			space();
			if (at < s.size() && (s[at] == '+' || s[at] == '-')) {
				const char op = s[at++];
				double r = 0.0;
				if (!term(r)) {
					return false;
				}
				v = op == '+' ? v + r : v - r;
			} else {
				return true;
			}
		}
	}
	bool term(double &v) {
		if (!factor(v)) {
			return false;
		}
		for (;;) {
			space();
			if (at < s.size() && (s[at] == '*' || s[at] == '/')) {
				const char op = s[at++];
				double r = 0.0;
				if (!factor(r)) {
					return false;
				}
				if (op == '/' && r == 0.0) {
					why = "a division by zero";
					return false;
				}
				v = op == '*' ? v * r : v / r;
			} else {
				return true;
			}
		}
	}
	bool factor(double &v) {
		space();
		if (at >= s.size()) {
			why = "the expression ends too soon";
			return false;
		}
		if (s[at] == '-') {
			++at;
			if (!factor(v)) {
				return false;
			}
			v = -v;
			return true;
		}
		if (s[at] == '(') {
			++at;
			if (!expression(v)) {
				return false;
			}
			space();
			if (at >= s.size() || s[at] != ')') {
				why = "a parenthesis is not closed";
				return false;
			}
			++at;
			return true;
		}
		if (std::isdigit(static_cast<unsigned char>(s[at])) || s[at] == '.') {
			size_t end = at;
			while (end < s.size() && (std::isdigit(static_cast<unsigned char>(s[end])) || s[end] == '.' || s[end] == 'e' ||
										(end > at && (s[end] == '-' || s[end] == '+') && (s[end - 1] == 'e')))) {
				++end;
			}
			v = std::strtod(std::string(s.substr(at, end - at)).c_str(), nullptr);
			at = end;
			return true;
		}
		size_t end = at;
		while (end < s.size() && (std::isalnum(static_cast<unsigned char>(s[end])) || s[end] == '_' || s[end] == '.')) {
			++end;
		}
		if (end == at) {
			why = std::string("unexpected '") + s[at] + "'";
			return false;
		}
		const std::string name(s.substr(at, end - at));
		at = end;
		const size_t dot = name.find('.');
		if (dot == std::string::npos) {
			why = "\"" + name + "\" is not item.what";
			return false;
		}
		Quantity q;
		q.item = name.substr(0, dot);
		const std::string rest = name.substr(dot + 1);
		const size_t second = rest.find('.');
		q.what = second == std::string::npos ? rest : rest.substr(0, second);
		q.measure = second == std::string::npos ? "g" : rest.substr(second + 1);
		const std::optional<double> got = measure(view, q);
		if (!got) {
			why = "\"" + name + "\" is not there";
			return false;
		}
		v = *got;
		return true;
	}
};

} // namespace

bool evaluate(std::string_view expression, const LabView &view, double &value, std::string &why) {
	Parser p{ expression, 0, view, {} };
	if (!p.expression(value)) {
		why = p.why;
		return false;
	}
	p.space();
	if (p.at != expression.size()) {
		why = "the expression has more after its end";
		return false;
	}
	return std::isfinite(value) || (why = "it is not a number", false);
}

} // namespace lab
