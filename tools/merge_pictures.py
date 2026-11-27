#!/usr/bin/env python3
"""The supplies' pictures captured bench by bench, side by side (Stage 84), merged into one file: every picture of
every file, by its key (a key is the supply's look and contents and the renderer's: the same key, the same picture),
sorted by key as chem_tool capture writes them (Ui::pictures). Every file with a picture was made by one renderer
(Stage 86: its "renderer"), or the merge refuses them.

  merge_pictures.py OUT IN...
"""
import json
import os
import sys


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    out, pictures, renderer = argv[0], {}, None
    for path in argv[1:]:
        try:
            with open(path) as f:
                j = json.load(f)
        except (OSError, ValueError) as e:
            print(f"merge_pictures: {path}: {e}", file=sys.stderr)
            return 1
        if j.get("format") != "chem-pictures/1":
            print(f"merge_pictures: {path} is not chem-pictures/1", file=sys.stderr)
            return 1
        if not j.get("pictures"):
            continue  # (none: a bench with no supplies, or none yet)
        if renderer is not None and j.get("renderer") != renderer:
            print(f"merge_pictures: {path} was made by renderer {j.get('renderer')}, the others by {renderer}", file=sys.stderr)
            return 1
        renderer = j.get("renderer")
        for p in j["pictures"]:
            pictures[p["key"]] = p
    merged = {"format": "chem-pictures/1"}
    if renderer is not None:
        merged["renderer"] = renderer
    merged["pictures"] = [pictures[k] for k in sorted(pictures)]
    text = json.dumps(merged, separators=(",", ":"))
    tmp = f"{out}.{os.getpid()}.tmp"
    with open(tmp, "w") as f:
        f.write(text)
    os.replace(tmp, out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
