#!/usr/bin/env python3
"""ctest pictures_store (Stage 93): tools/pictures.py against a store of its own, on two small benches that share a
supply. Cold, each distinct picture captured once; again, none, the file the same; another tree's file from the same
store, none captured and the same file; a picture lost from the store, that one alone captured again; a renderer
unused past --days removed, a recent one kept.

  test_pictures.py CHEM_TOOL
"""
import json
import os
import re
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BEAKER = {"id": "beaker", "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "beaker", "wall_g": 80, "contents": {"water": {"g": 100}}}
FLASK = {"id": "flask", "kind": "vessel", "volume_ml": 250, "wall": "glass", "glassware": "conical_flask", "wall_g": 100, "contents": {"water": {"g": 50}}}


def bench(items):
    return {"format": "chem-bench/2", "room": {"celsius": 20, "air_mol": 1000}, "items": items}


def run(tool, store, out, benches, days=30):
    r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "pictures.py"), "--tool", tool, "--store", store, "--out", out, "--jobs", "4",
                        "--days", str(days)] + benches, capture_output=True, text=True)
    if r.returncode != 0:
        raise AssertionError(f"pictures.py failed: {r.stderr}")
    m = re.search(r"(\d+) supplies in (\d+) benches, (\d+) pictures, (\d+) captured, renderer (\w+)", r.stdout)
    if not m:
        raise AssertionError(f"pictures.py said: {r.stdout}")
    with open(out) as f:
        text = f.read()
    return [int(m.group(i)) for i in range(1, 5)], m.group(5), text, r.stdout


def check(what, ok):
    print(("ok   " if ok else "FAIL ") + what)
    return ok


def main(argv):
    tool = argv[0]
    good = True
    with tempfile.TemporaryDirectory() as tmp:
        store = os.path.join(tmp, "store")
        a, b = os.path.join(tmp, "a.json"), os.path.join(tmp, "b.json")
        with open(a, "w") as f:
            json.dump(bench([BEAKER]), f)
        with open(b, "w") as f:
            json.dump(bench([BEAKER, FLASK]), f)
        counts, renderer, cold, _ = run(tool, store, os.path.join(tmp, "tree1", "supplies.json"), [a, b])
        good &= check(f"cold: 3 supplies in 2 benches, 2 pictures, both captured ({counts})", counts == [3, 2, 2, 2])
        j = json.loads(cold)
        good &= check("the file: this renderer's, its 2 pictures sorted by key",
                      j.get("renderer") == renderer and [p["key"] for p in j["pictures"]] == sorted(p["key"] for p in j["pictures"]) and len(j["pictures"]) == 2)
        good &= check("the store: a picture a file, under the renderer", sorted(os.listdir(os.path.join(store, renderer))) == sorted(f"{p['key']}.json" for p in j["pictures"]))
        counts, _, warm, _ = run(tool, store, os.path.join(tmp, "tree1", "supplies.json"), [a, b])
        good &= check(f"again: none captured, the same file ({counts})", counts[3] == 0 and warm == cold)
        counts, _, other, _ = run(tool, store, os.path.join(tmp, "tree2", "supplies.json"), [a, b])
        good &= check(f"another tree, the same store: none captured, the same file ({counts})", counts[3] == 0 and other == cold)
        lost = j["pictures"][0]["key"]
        os.remove(os.path.join(store, renderer, f"{lost}.json"))
        counts, _, again, _ = run(tool, store, os.path.join(tmp, "tree1", "supplies.json"), [a, b])
        good &= check(f"a picture lost from the store: it alone captured, the same file ({counts})", counts[3] == 1 and again == cold)
        old, recent = os.path.join(store, "0" * 16), os.path.join(store, "1" * 16)
        for path, days in ((old, 40), (recent, 5)):
            os.makedirs(path)
            t = time.time() - days * 86400
            os.utime(path, (t, t))
        _, _, _, said = run(tool, store, os.path.join(tmp, "tree1", "supplies.json"), [a, b])
        good &= check("a renderer unused 40 days removed, one used 5 days ago kept, this one kept",
                      not os.path.exists(old) and os.path.exists(recent) and os.path.isdir(os.path.join(store, renderer)) and "removed" in said)
    return 0 if good else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
