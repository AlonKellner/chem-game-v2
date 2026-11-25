#pragma once

#include <cctype>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Chemical formulas and equations as the data files spell them (data/species.json,
// data/reactions.json):
//
//   formula  := part ('.' part)*            "CuSO4.5H2O": a hydrate's parts add
//   part     := [count] group+              a leading count multiplies the part
//   group    := (Element | '(' group+ ')') [count]
//   Element  := an upper-case letter, then lower-case letters
//
//   equation := side "->" side
//   side     := term (" + " term)*
//   term     := [count] formula '(' phase ')'      phase: s | l | g | aq
namespace chemdata {

using AtomCounts = std::map<std::string, int>;

namespace detail {

struct Cursor {
	std::string_view text;
	size_t at = 0;
	bool done() const { return at >= text.size(); }
	char peek() const { return done() ? '\0' : text[at]; }
};

inline bool is_digit(char c) {
	return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

// a count of at most 6 digits; 1 when there is none
inline bool parse_count(Cursor &c, int &count, std::string &why) {
	count = 1;
	if (!is_digit(c.peek())) {
		return true;
	}
	int value = 0;
	int digits = 0;
	while (is_digit(c.peek())) {
		value = value * 10 + (c.peek() - '0');
		++c.at;
		if (++digits > 6) {
			why = "a count is too large";
			return false;
		}
	}
	if (value == 0) {
		why = "a count of zero";
		return false;
	}
	count = value;
	return true;
}

inline void add_scaled(AtomCounts &into, const AtomCounts &from, int scale) {
	for (const auto &[symbol, n] : from) {
		into[symbol] += n * scale;
	}
}

inline bool parse_groups(Cursor &c, AtomCounts &out, std::string &why, int depth) {
	bool any = false;
	while (!c.done()) {
		AtomCounts group;
		const char ch = c.peek();
		if (std::isupper(static_cast<unsigned char>(ch)) != 0) {
			std::string symbol(1, ch);
			++c.at;
			while (std::islower(static_cast<unsigned char>(c.peek())) != 0) {
				symbol += c.peek();
				++c.at;
			}
			group[symbol] = 1;
		} else if (ch == '(') {
			if (depth >= 8) {
				why = "parentheses nested too deeply";
				return false;
			}
			++c.at;
			if (!parse_groups(c, group, why, depth + 1)) {
				return false;
			}
			if (c.peek() != ')') {
				why = "a '(' without its ')'";
				return false;
			}
			++c.at;
		} else {
			break;
		}
		int count = 1;
		if (!parse_count(c, count, why)) {
			return false;
		}
		add_scaled(out, group, count);
		any = true;
	}
	if (!any) {
		why = "an element was expected";
		return false;
	}
	return true;
}

inline std::string_view trim(std::string_view s) {
	while (!s.empty() && s.front() == ' ') {
		s.remove_prefix(1);
	}
	while (!s.empty() && s.back() == ' ') {
		s.remove_suffix(1);
	}
	return s;
}

} // namespace detail

// The atoms of a formula. false, with the reason in `why`, when it does not parse.
inline bool parse_formula(std::string_view text, AtomCounts &out, std::string &why) {
	out.clear();
	detail::Cursor c{ text };
	if (c.done()) {
		why = "an empty formula";
		return false;
	}
	for (;;) {
		int scale = 1;
		if (!detail::parse_count(c, scale, why)) {
			return false;
		}
		AtomCounts part;
		if (!detail::parse_groups(c, part, why, 0)) {
			return false;
		}
		detail::add_scaled(out, part, scale);
		if (c.done()) {
			return true;
		}
		if (c.peek() != '.') {
			why = std::string("unexpected '") + c.peek() + "'";
			return false;
		}
		++c.at;
	}
}

struct EquationTerm {
	std::string species; // "H2O(g)": formula and phase, as the species table keys it
	int nu = 0; // negative on the left of the arrow
};

// The terms of an equation. false, with the reason in `why`, when it does not parse.
inline bool parse_equation(std::string_view text, std::vector<EquationTerm> &out, std::string &why) {
	out.clear();
	const size_t arrow = text.find("->");
	if (arrow == std::string_view::npos || text.find("->", arrow + 2) != std::string_view::npos) {
		why = "an equation has exactly one '->'";
		return false;
	}
	const std::string_view sides[2] = { text.substr(0, arrow), text.substr(arrow + 2) };
	for (int side = 0; side < 2; ++side) {
		std::string_view rest = sides[side];
		for (;;) {
			const size_t plus = rest.find(" + ");
			std::string_view term = detail::trim(rest.substr(0, plus));
			detail::Cursor c{ term };
			int count = 1;
			if (!detail::parse_count(c, count, why)) {
				return false;
			}
			const std::string_view species = detail::trim(term.substr(c.at));
			if (species.empty()) {
				why = "an empty term";
				return false;
			}
			out.push_back(EquationTerm{ std::string(species), side == 0 ? -count : count });
			if (plus == std::string_view::npos) {
				break;
			}
			rest = rest.substr(plus + 3);
		}
	}
	return true;
}

} // namespace chemdata
