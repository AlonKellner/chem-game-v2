#!/usr/bin/env python3
"""The supplies' pictures captured bench by bench, side by side (Stage 84), merged into one file: every picture of
every file, by its key (a key is the supply's look and contents: the same key, the same picture), sorted by key as
chem_tool capture writes them (Ui::pictures).

  merge_pictures.py OUT IN...
"""
import json
import os
import sys


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    out, pictures = argv[0], {}
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
        for p in j.get("pictures", []):
            pictures[p["key"]] = p
    text = json.dumps({"format": "chem-pictures/1", "pictures": [pictures[k] for k in sorted(pictures)]}, separators=(",", ":"))
    tmp = f"{out}.{os.getpid()}.tmp"
    with open(tmp, "w") as f:
        f.write(text)
    os.replace(tmp, out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
