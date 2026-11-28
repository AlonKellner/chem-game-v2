#!/usr/bin/env python3
"""The supplies' pictures built ahead (Stage 61), from a store every worktree shares (Stage 93).

A picture's key is a hash of the renderer's (RENDERER_KEY, the files it is rendered by) and of everything drawn of the
supply (Ui's picture_key): the same key, the same picture, in any tree. So the store keeps each picture by its key,
~/Library/Caches/chem-game-v2/pictures/RENDERER/KEY.json (XDG_CACHE_HOME elsewhere; PICTURES_STORE or --store over
it), and a tree captures only what no tree has: a supply in several benches once, a renderer a branch changed and main
merged already there, one taken back still there. Each missing picture is captured alone, side by side
(`chem_tool capture --only`), and written into the store as a whole file or not at all; the tree's file (what the Godot
host reads) is assembled from the store, this renderer's pictures of the benches given, sorted by key (as Ui::pictures
writes them). A renderer's directory is touched whenever it is used; one unused for --days (30) is removed.

  pictures.py --tool CHEM_TOOL --out FILE [--store DIR] [--jobs N] [--days D] BENCH...
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor


def default_store():
    if os.environ.get("PICTURES_STORE"):
        return os.environ["PICTURES_STORE"]
    if sys.platform == "darwin":
        base = os.path.expanduser("~/Library/Caches")
    else:
        base = os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache")
    return os.path.join(base, "chem-game-v2", "pictures")


def write_whole(path, text):
    """Written to a file of its own and renamed over: a reader sees the old file or the new, never a part."""
    tmp = f"{path}.{os.getpid()}.{time.monotonic_ns()}.tmp"
    with open(tmp, "w") as f:
        f.write(text)
    os.replace(tmp, path)


def keys_of(tool, benches, jobs):
    """Each bench's supplies and their keys: [(bench, renderer, [(id, key)])], a bench a process."""
    def one(bench):
        r = subprocess.run([tool, "capture", "--keys", bench], capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError(f"chem_tool capture --keys {bench}: {r.stderr.strip()}")
        out = []
        for line in r.stdout.splitlines():
            path, _, text = line.partition("\t")
            j = json.loads(text)
            out.append((path, j["renderer"], [(s["id"], s["key"]) for s in j["supplies"]]))
        return out
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        return [row for rows in pool.map(one, benches) for row in rows]


def capture(tool, bench, supply, key, renderer, folder):
    """One supply's picture, alone, into the store under its key."""
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "picture.json")
        r = subprocess.run([tool, "capture", "--only", supply, out, bench], capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError(f"chem_tool capture --only {supply} {bench}: {r.stderr.strip()}")
        with open(out) as f:
            j = json.load(f)
    if j.get("renderer") != renderer:
        raise RuntimeError(f"{bench}: {supply}'s picture came from renderer {j.get('renderer')}, not {renderer}")
    got = {p["key"]: p for p in j.get("pictures", [])}
    if key not in got:
        raise RuntimeError(f"{bench}: {supply}'s picture came as {sorted(got)}, not {key}")
    write_whole(os.path.join(folder, f"{key}.json"), json.dumps(got[key], separators=(",", ":")))


def prune(store, renderer, days):
    """Each renderer's directory unused for `days`, removed (this one's touched now)."""
    os.utime(os.path.join(store, renderer))
    gone = []
    for name in os.listdir(store):
        path = os.path.join(store, name)
        if name != renderer and os.path.isdir(path) and time.time() - os.path.getmtime(path) > days * 86400.0:
            shutil.rmtree(path, ignore_errors=True)
            gone.append(name)
    return gone


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tool", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--store", default=None)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--days", type=float, default=30.0)
    ap.add_argument("benches", nargs="+")
    a = ap.parse_args(argv)
    store = a.store or default_store()
    start = time.monotonic()
    rows = keys_of(a.tool, a.benches, a.jobs)
    renderers = {renderer for _, renderer, supplies in rows if supplies}
    if len(renderers) > 1:
        print(f"pictures: the benches give renderers {sorted(renderers)}", file=sys.stderr)
        return 1
    renderer = renderers.pop() if renderers else None
    wanted = {}  # key -> (bench, supply): the first bench that has it
    for bench, _, supplies in rows:
        for supply, key in supplies:
            wanted.setdefault(key, (bench, supply))
    captured, gone = 0, []
    pictures = []
    if renderer is not None:
        folder = os.path.join(store, renderer)
        os.makedirs(folder, exist_ok=True)
        missing = [(key, bench, supply) for key, (bench, supply) in wanted.items() if not os.path.exists(os.path.join(folder, f"{key}.json"))]
        with ThreadPoolExecutor(max_workers=a.jobs) as pool:
            for f in [pool.submit(capture, a.tool, bench, supply, key, renderer, folder) for key, bench, supply in missing]:
                f.result()
        captured = len(missing)
        for key in sorted(wanted):
            with open(os.path.join(folder, f"{key}.json")) as f:
                pictures.append(json.load(f))
        gone = prune(store, renderer, a.days)
    merged = {"format": "chem-pictures/1"}
    if renderer is not None:
        merged["renderer"] = renderer
    merged["pictures"] = pictures
    text = json.dumps(merged, separators=(",", ":"))
    try:
        with open(a.out) as f:
            same = f.read() == text
    except OSError:
        same = False
    if not same:
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        write_whole(a.out, text)
    supplies = sum(len(s) for _, _, s in rows)
    print(f"pictures: {supplies} supplies in {len(rows)} benches, {len(wanted)} pictures, {captured} captured, renderer {renderer}, "
          f"{time.monotonic() - start:.1f} s{'; removed ' + ', '.join(gone) if gone else ''}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
