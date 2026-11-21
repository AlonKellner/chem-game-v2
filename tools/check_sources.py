#!/usr/bin/env python3
"""Every number in data/ has a known origin (CLAUDE.md, 'Fetched data is documented and held to
its source'): the ctest entries data_sources and data_sources_self_test.

data/sources.json maps each value to an entry: measured (the tests that hold it to its source, and
the phrases docs/DATA_SOURCES.md must contain), calibrated (why, the doc's phrases) or design (why).
This fails when a value matches no entry, an entry matches nothing (stale), a named test does not
exist, a phrase is missing from the doc, or a pattern hides a new file or item behind a wildcard.

  check_sources.py              # check the repository
  check_sources.py --self-test  # prove each failure fires on a corrupted copy
"""
import copy
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "data")
REGISTRY = "sources.json"
NUMBER = re.compile(r"^-?\d+(\.\d+)?([eE][-+]?\d+)?$")
KEYS = ("id", "symbol", "name")
KINDS = ("measured", "calibrated", "design")
TEST_CASE = re.compile(r'TEST_CASE\("((?:[^"\\]|\\.)*)"')
GUT_TEST = re.compile(r"^func (test_\w+)", re.M)


def leaves(value, path, out):
	"""The numeric values under value, by path."""
	if isinstance(value, dict):
		for k, v in value.items():
			leaves(v, path + [k], out)
	elif isinstance(value, list):
		for i, v in enumerate(value):
			name = next((v[k] for k in KEYS if isinstance(v, dict) and isinstance(v.get(k), str)), None)
			leaves(v, path + [name if name is not None else "#"], out)
	elif isinstance(value, bool):
		return
	elif isinstance(value, (int, float)) or (isinstance(value, str) and NUMBER.match(value)):
		out.add("/".join(path))


def data_leaves(files):
	"""files: {relative path: parsed JSON, or None for a CSV table}."""
	out = set()
	for rel, parsed in files.items():
		if rel == REGISTRY:
			continue
		if parsed is None:
			out.add(rel)
		else:
			leaves(parsed, [rel], out)
	return out


def read_data():
	files = {}
	for path in sorted(glob.glob(os.path.join(DATA, "**", "*"), recursive=True)):
		rel = os.path.relpath(path, DATA).replace(os.sep, "/")
		if path.endswith(".json"):
			with open(path, encoding="utf-8") as f:
				files[rel] = json.load(f)
		elif path.endswith(".csv"):
			files[rel] = None
	return files


def segment_regex(segment):
	if segment == "#":
		return r"#"
	if segment.startswith("{") and segment.endswith("}"):
		return "(?:" + "|".join(re.escape(s) for s in segment[1:-1].split(",")) + ")"
	return re.escape(segment)


def expand(pattern):
	"""The pattern once for each combination of its '{a,b}' names."""
	out = [""]
	for i, part in enumerate(pattern.split("/")):
		names = part[1:-1].split(",") if part.startswith("{") and part.endswith("}") else [part]
		out = [(o + "/" if i else "") + n for o in out for n in names]
	return out


def compile_pattern(pattern):
	"""A path pattern as a regex, or the reason it is refused."""
	parts = pattern.split("/")
	out = []
	for i, part in enumerate(parts):
		last = i == len(parts) - 1
		if part in ("*", "**"):
			if not last:
				return None, f"'{pattern}': '{part}' only as the last segment, so a new item is entered by name"
			if i < 2:
				return None, f"'{pattern}': a wildcard below a file's own keys, not over files"
			out.append(r"[^/]+" if part == "*" else r".+")
		elif "*" in part or "?" in part:
			return None, f"'{pattern}': a wildcard inside a name"
		else:
			out.append(segment_regex(part))
	return re.compile("^" + "/".join(out) + "$"), None


def test_names(roots):
	names = []
	for root in roots:
		for path in glob.glob(os.path.join(root, "**", "*.cpp"), recursive=True):
			with open(path, encoding="utf-8") as f:
				names += [n.replace('\\"', '"') for n in TEST_CASE.findall(f.read())]
		for path in glob.glob(os.path.join(root, "**", "test_*.gd"), recursive=True):
			with open(path, encoding="utf-8") as f:
				names += GUT_TEST.findall(f.read())
	return names


def check(files, registry, names, doc):
	"""The failures, as sentences."""
	errors = []
	entries = registry.get("entries", [])
	values = data_leaves(files)
	covered = set()
	for n, entry in enumerate(entries):
		label = f"entry {n} ({(entry.get('data') or ['?'])[0]})"
		kinds = [k for k in KINDS if k in entry]
		if len(kinds) != 1 or not isinstance(entry[kinds[0]], str) or not entry[kinds[0]].strip():
			errors.append(f"{label}: exactly one of {', '.join(KINDS)}, saying where it comes from")
			continue
		kind = kinds[0]
		if kind != "design":
			phrases = entry.get("doc", [])
			if not phrases:
				errors.append(f"{label}: a {kind} value is documented: give the phrases of its row in docs/DATA_SOURCES.md")
			for phrase in phrases:
				if phrase not in doc:
					errors.append(f"{label}: docs/DATA_SOURCES.md does not contain '{phrase}'")
		tests = entry.get("tests", [])
		if kind == "measured" and not tests:
			errors.append(f"{label}: a measured value is held to its source by a test: name it")
		for t in tests:
			if not any(name.startswith(t) for name in names):
				errors.append(f"{label}: no test is named '{t}...'")
		for pattern in entry.get("data", []):
			regex, why = compile_pattern(pattern)
			if regex is None:
				errors.append(f"{label}: {why}")
				continue
			covered |= {v for v in values if regex.match(v)}
			# each of its names, alone: a misspelt or vanished one is stale too
			for single in expand(pattern):
				regex, _ = compile_pattern(single)
				if not any(regex.match(v) for v in values):
					errors.append(f"{label}: '{single}' matches no value in data/ (stale)")
	for v in sorted(values - covered):
		errors.append(f"data/{v}: no entry in data/{REGISTRY} says where it comes from")
	return errors


def repository():
	files = read_data()
	with open(os.path.join(DATA, REGISTRY), encoding="utf-8") as f:
		registry = json.load(f)
	with open(os.path.join(ROOT, "docs", "DATA_SOURCES.md"), encoding="utf-8") as f:
		doc = f.read()
	names = test_names([os.path.join(ROOT, "native", "tests"), os.path.join(ROOT, "engines", "godot", "tests")])
	return files, registry, names, doc


def self_test():
	files, registry, names, doc = repository()
	assert not check(files, registry, names, doc), "the repository itself must pass first"
	cases = []
	# a new measured value with no entry
	f = copy.deepcopy(files)
	f["species.json"]["species"].append({"id": "ozone", "formula": "O3", "phase": "g", "thermo": {"hf_kj": 142.7, "cp": 39.2, "s0": 238.9}})
	cases.append(("a new species", f, registry, names, doc, "data/species.json/species/ozone/thermo/hf_kj: no entry"))
	# a new table
	f = copy.deepcopy(files)
	f["optics/ozone.csv"] = None
	cases.append(("a new table", f, registry, names, doc, "data/optics/ozone.csv: no entry"))
	# a new field of a known item
	f = copy.deepcopy(files)
	f["benches/demo.json"]["items"][0]["lip_mm"] = 2.0
	cases.append(("a new field", f, registry, names, doc, "lip_mm: no entry"))
	# a named test removed
	cases.append(("a missing test", files, registry, [n for n in names if not n.startswith("measured: malachite at Koga")], doc, "no test is named 'measured: malachite at Koga"))
	# the doc's row removed
	cases.append(("a missing row", files, registry, names, doc.replace("GDS801", "G"), "does not contain 'GDS801'"))
	# a measured entry with no test
	r = copy.deepcopy(registry)
	next(e for e in r["entries"] if "Majima" in e.get("measured", ""))["tests"] = []
	cases.append(("a measured value untested", files, r, names, doc, "held to its source by a test"))
	# a wildcard over items
	r = copy.deepcopy(registry)
	r["entries"].append({"data": ["species.json/species/*/thermo/*"], "design": "everything"})
	cases.append(("a wildcard over items", files, r, names, doc, "only as the last segment"))
	# a stale entry
	r = copy.deepcopy(registry)
	r["entries"].append({"data": ["species.json/species/ozone/thermo/*"], "design": "gone"})
	cases.append(("a stale entry", files, r, names, doc, "matches no value in data/ (stale)"))
	# a misspelt name among others that match
	r = copy.deepcopy(registry)
	r["entries"].append({"data": ["species.json/species/{water,watr}/density"], "design": "a typo"})
	cases.append(("a misspelt name", files, r, names, doc, "'species.json/species/watr/density' matches no value"))
	failed = 0
	for what, f, r, n, d, expected in cases:
		errors = check(f, r, n, d)
		if not any(expected in e for e in errors):
			print(f"self-test: {what} was not detected (expected '{expected}'; got {errors})")
			failed += 1
	print(f"check_sources self-test: {len(cases) - failed} of {len(cases)} corruptions detected")
	return 1 if failed else 0


def main():
	if "--self-test" in sys.argv[1:]:
		return self_test()
	files, registry, names, doc = repository()
	errors = check(files, registry, names, doc)
	for e in errors:
		print(e)
	values = data_leaves(files)
	print(f"check_sources: {len(values)} values in data/, {len(registry.get('entries', []))} entries, {len(errors)} failures")
	return 1 if errors else 0


if __name__ == "__main__":
	sys.exit(main())
