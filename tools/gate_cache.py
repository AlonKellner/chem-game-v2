#!/usr/bin/env python3
"""The gates' result cache (docs/WORKFLOW.md, "The result cache").

A test whose inputs hash the same as on a run where it passed passes again: the
native tests are deterministic functions of their binary, their command line,
their environment and the repo data they read. So `make gate` reruns only what a
change can affect. A change to the game layer leaves `chem_tests` (the core alone)
byte for byte the same, and its sweeps and goldens are not run again.

Two levels (Stage 68), GATE_LEVEL=commit (the default) or merge:
  - a pass at the merge level (`make gates`) is kept as one, and as a commit pass;
  - `make gates` takes only a merge-level pass for its own, `make gate` either.
So a `make gates` run again after one entry failed, or on main after a merge whose
binaries are the branch's, runs only what is not known to pass. GATE_CACHE=0 runs
everything (and still records what passed).

One cache for every worktree (Stage 68): GATE_CACHE_FILE, by default
~/Library/Caches/chem-game-v2/gate_cache.json (XDG_CACHE_HOME elsewhere), read and
written under a lock, so gates in several worktrees share what passed.

  gate_cache.py ctest BUILD_DIR [--jobs N] [--regex RE] [--no-cache] [--limit COSTS:SECONDS ...]
      ctest in BUILD_DIR, skipping the entries that passed with the same key.
      An entry is cacheable when its command is a binary of BUILD_DIR. Its key
      covers that binary and the shared libraries it links from BUILD_DIR, its
      arguments and ENVIRONMENT property, the CHEM_*/LAB_* variables of this
      process, and DATA (the repo data the tests read). Others (lints, CMake
      scripts) always run.
  gate_cache.py step NAME --inputs PATH... -- COMMAND...
      runs COMMAND unless it passed with the same inputs. A PATH is a repo
      directory or file (tracked content via git, edits and untracked files by
      content) or file:GLOB (untracked build products, by content).
  gate_cache.py stepcost BINARY SESSION
      (Stage 75) the simulation's step cost: `BINARY bench SESSION`'s instructions
      a step, kept in step_costs.json beside the cache by the binary's content and
      the data (an unchanged binary is not run again), with what it was built from
      (native, data, bench) when that has no edits. Printed against the count for
      what main built from where this branch left it (main's own: its first
      parent's; a commit of documents alone changes neither), with a `!!`
      line above GATE_STEPCOST_WARN percent (10). It fails only if the bench does.
  gate_cache.py clear
"""
import glob
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def cache_file():
    if os.environ.get("GATE_CACHE_FILE"):
        return os.environ["GATE_CACHE_FILE"]
    if sys.platform == "darwin":
        base = os.path.expanduser("~/Library/Caches")
    else:
        base = os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache")
    return os.path.join(base, "chem-game-v2", "gate_cache.json")


CACHE = cache_file()
LEVEL = os.environ.get("GATE_LEVEL", "commit")
KEEP = 16  # passing keys kept per entry (a revert, a branch switch back, another worktree: still cached)
# what the native tests read from the repo (CHEM_REPO_ROOT)
DATA = ["data", "bench"]


def sha_file(path, memo={}):
    if path not in memo:
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        memo[path] = h.hexdigest()
    return memo[path]


def sha_binary(path, memo={}):
    """A built binary as it behaves (Stage 68): of a Mach-O binary, its sections' names and contents (its code and
    data), not its header nor its link-edit data, where its debug map names each object by its whole path, the
    worktree's among them, and its UUID and signature follow from those. So the same commit's binary in two
    worktrees reads the same here. Any other file: its whole content."""
    import struct
    if path not in memo:
        with open(path, "rb") as f:
            content = f.read()
        h = hashlib.sha256()
        if content[:4] == b"\xcf\xfa\xed\xfe":  # MH_MAGIC_64, little-endian
            ncmds, = struct.unpack_from("<I", content, 16)
            at = 32
            for _ in range(ncmds):
                cmd, size = struct.unpack_from("<II", content, at)
                if cmd == 0x19:  # LC_SEGMENT_64
                    segname = content[at + 8:at + 24]
                    nsects, = struct.unpack_from("<I", content, at + 64)
                    if not segname.startswith(b"__LINKEDIT"):
                        for k in range(nsects):
                            sec = at + 72 + 80 * k
                            names = content[sec:sec + 32]
                            sec_size, = struct.unpack_from("<Q", content, sec + 40)
                            offset, = struct.unpack_from("<I", content, sec + 48)
                            flags, = struct.unpack_from("<I", content, sec + 64)
                            h.update(names + struct.pack("<Q", sec_size))
                            if offset != 0 and (flags & 0xff) not in (0x1, 0xc, 0x12):  # zerofill sections hold no bytes
                                h.update(content[offset:offset + sec_size])
                at += size
        else:
            h.update(content)
        memo[path] = h.hexdigest()
    return memo[path]


def git(*args, root=None):
    return subprocess.run(["git", "-C", root or ROOT, *args], check=True, capture_output=True, text=True).stdout


def fingerprint(paths, root=None):
    """The content of repo paths as the working tree has it: each file's blob id, the index's where the file is as
    the index has it, the id git would give its content where it is edited or untracked; a deleted file left out.
    So it is the same before and after the same content is staged and committed: a gate, then the commit, then the
    next gate, and what passed is still known (it was not, 2026-11-28: the index's ids and the edited files' own
    hashes were two spellings of one tree, and a commit that touched data/ ran every entry again)."""
    root = root or ROOT
    h = hashlib.sha256()
    repo = [p for p in paths if not p.startswith("file:")]
    if repo:
        blobs = {}
        for row in git("ls-files", "-s", "-z", "--", *repo, root=root).split("\0"):
            if row:
                meta, path = row.split("\t", 1)
                if meta.split()[2] == "0":  # (not a side of a merge left unresolved: those are read as edits, below)
                    blobs[path] = meta.split()[1]
        changed = set(git("diff", "--name-only", "-z", "--", *repo, root=root).split("\0"))
        changed |= set(git("ls-files", "-o", "--exclude-standard", "-z", "--", *repo, root=root).split("\0"))
        there = sorted(c for c in changed if c and os.path.isfile(os.path.join(root, c)))
        for c in changed:
            blobs.pop(c, None)
        for at in range(0, len(there), 200):
            part = there[at:at + 200]
            ids = git("hash-object", "--", *part, root=root).split()
            blobs.update(zip(part, ids))
        for path in sorted(blobs):
            h.update(f"{blobs[path]}\t{path}\n".encode())
    for spec in sorted(p[5:] for p in paths if p.startswith("file:")):
        for f in sorted(glob.glob(os.path.join(root, spec))):
            h.update(f"\n{os.path.relpath(f, root)}:{sha_file(f)}".encode())
        h.update(f"\n{spec}".encode())
    return h.hexdigest()


def rc_env():
    return sorted((k, v) for k, v in os.environ.items() if k.startswith(("CHEM_", "LAB_")))


def linked_libs(binary, build):
    """The shared libraries of BUILD_DIR the binary links (the C API's)."""
    try:
        out = subprocess.run(["otool", "-L", binary], capture_output=True, text=True).stdout
    except FileNotFoundError:
        out = subprocess.run(["ldd", binary], capture_output=True, text=True).stdout
    libs = []
    for name in sorted(set(re.findall(r"(lib[\w.+-]+\.(?:dylib|so[\d.]*))", out))):
        for f in glob.glob(os.path.join(build, "**", name), recursive=True):
            libs.append(f)
    return sorted(libs)


def load():
    try:
        with open(CACHE) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def keys_of(cache, entry):
    v = cache.get(entry, [])
    return [v] if isinstance(v, str) else v


def passed(cache, entry, key):
    """Passed at this run's level: a merge run takes only a merge-level pass."""
    return key in keys_of(cache, ("merge|" if LEVEL == "merge" else "") + entry)


def record_one(cache, entry, key, ok):
    keys = [k for k in keys_of(cache, entry) if k != key]
    cache[entry] = ([key] + keys)[:KEEP] if ok else keys


def record(cache, entry, key, ok):
    record_one(cache, entry, key, ok)
    if LEVEL == "merge" or not ok:
        record_one(cache, "merge|" + entry, key, ok)


def update(changes):
    """Applies changes(cache) to the cache on disk under a lock: gates in other
    worktrees write it too."""
    import fcntl
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    with open(CACHE + ".lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        cache = load()
        changes(cache)
        tmp = f"{CACHE}.{os.getpid()}.tmp"
        with open(tmp, "w") as f:
            json.dump(cache, f, indent=0, sort_keys=True)
        os.replace(tmp, CACHE)


def entry_of(build, name):
    """A ctest entry by its tree's kind (build-core, build-asan) and name."""
    return f"ctest:{os.path.basename(build)}:{name}"


COSTS = os.path.join(os.path.dirname(CACHE), "costs")  # (Stage 68) ctest's measured costs, a tree's kind a file


def tree_costs(build):
    return os.path.join(build, "Testing", "Temporary", "CTestCostData.txt")


def shared_costs(kind):
    return os.path.join(COSTS, kind + ".txt")


def read_cost_file(path):
    """A CTestCostData.txt: its entries (name -> (runs, seconds)) and what follows its '---' (the last failures)."""
    entries, tail = {}, ""
    try:
        text = open(path).read()
    except OSError:
        return entries, tail
    head, sep, rest = text.partition("---")
    for line in head.splitlines():
        parts = line.split()
        if len(parts) == 3:
            try:
                entries[parts[0]] = (int(parts[1]), float(parts[2]))
            except ValueError:
                pass
    return entries, (sep + rest if sep else "")


def write_cost_file(path, entries, tail):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = f"{path}.{os.getpid()}.tmp"
    with open(tmp, "w") as f:
        for name in sorted(entries):
            runs, seconds = entries[name]
            f.write(f"{name} {runs} {seconds:g}\n")
        f.write(tail if tail else "---\n")
    os.replace(tmp, path)


def seed_costs(build):
    """(Stage 70) A tree's costs, its own and, for what it has not measured, what other worktrees' trees of its kind
    measured: a new worktree's ctest runs the longest first, and the gates' limits leave out the same entries."""
    kind = os.path.basename(build)
    own, tail = read_cost_file(tree_costs(build))
    shared, _ = read_cost_file(shared_costs(kind))
    missing = {k: v for k, v in shared.items() if k not in own}
    if missing:
        own.update(missing)
        write_cost_file(tree_costs(build), own, tail)


def share_costs(build):
    """(Stage 70) A tree's measured costs into its kind's shared file, under the cache's lock."""
    import fcntl
    kind = os.path.basename(build)
    own, _ = read_cost_file(tree_costs(build))
    if not own:
        return
    os.makedirs(COSTS, exist_ok=True)
    with open(CACHE + ".lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        shared, tail = read_cost_file(shared_costs(kind))
        shared.update(own)
        write_cost_file(shared_costs(kind), shared, tail)


def costs_of(path):
    """ctest's measured seconds an entry (Testing/Temporary/CTestCostData.txt: name, runs, seconds)."""
    out = {}
    try:
        for line in open(path):
            if line.startswith("---"):
                break
            parts = line.split()
            if len(parts) == 3:
                out[parts[0]] = float(parts[2])
    except OSError:
        pass
    return out


def ctest(build, jobs, regex, use_cache, limits=()):
    build = os.path.abspath(build)
    # (Stage 68) --limit FILE:S, as many as asked: no entry that took more than S seconds where FILE measured it
    # (ctest's CTestCostData.txt, a tree's); one no FILE has measured runs
    seed_costs(build)
    # a limit names a tree's costs by its file, or by its kind (build-core, build-asan): then this worktree's tree
    # of that kind and every other worktree's (the shared file)
    def costs_for(spec):
        if os.path.sep in spec or spec.endswith(".txt"):
            return costs_of(spec)
        merged = costs_of(shared_costs(spec))
        merged.update(costs_of(tree_costs(os.path.join(os.path.dirname(build), spec))))
        return merged
    measured = [(costs_for(f), most) for f, most in limits if most > 0]
    too_long = lambda name: any(m.get(name, 0.0) > most for m, most in measured)
    left = []
    listing = json.loads(subprocess.run(["ctest", "--test-dir", build, "--show-only=json-v1"], check=True,
                                        capture_output=True, text=True).stdout)
    # an entry left out takes the fixtures it sets up with it, and every entry that needs one of them: ctest would
    # run a fixture's setup for any entry that needs it
    gone = set()
    if measured:
        for t in listing["tests"]:
            props = {p["name"]: p["value"] for p in t.get("properties", [])}
            if too_long(t["name"]):
                gone.update(props.get("FIXTURES_SETUP", []) or [])
    data = fingerprint(DATA)
    env = rc_env()
    cache = load()
    keys, run, skipped = {}, [], []
    lib_memo = {}
    for t in listing["tests"]:
        name = t["name"]
        if regex and not re.search(regex, name):
            continue
        if measured and (too_long(name) or
                         any(f in gone for f in ({p["name"]: p["value"] for p in t.get("properties", [])}.get("FIXTURES_REQUIRED", []) or []))):
            left.append(name)
            continue
        cmd = t.get("command") or []
        props = {p["name"]: p["value"] for p in t.get("properties", [])}
        if cmd and os.path.isfile(cmd[0]) and os.path.abspath(cmd[0]).startswith(build + os.sep):
            if cmd[0] not in lib_memo:
                lib_memo[cmd[0]] = [(os.path.relpath(l, build), sha_binary(l)) for l in linked_libs(cmd[0], build)]
            key = hashlib.sha256(json.dumps([sha_binary(cmd[0]), lib_memo[cmd[0]], cmd[1:], props.get("ENVIRONMENT"),
                                             env, data]).encode()).hexdigest()
            keys[name] = key
            if use_cache and passed(cache, entry_of(build, name), key):
                skipped.append(name)
                continue
        run.append(name)
    print(f"gate_cache: {len(run)} ctest entries to run, {len(skipped)} unchanged since they passed"
          + (f" at the {LEVEL} level" if use_cache else " (cache off)") + f" ({os.path.basename(build)})"
          + (f"; {len(left)} left to the full gates, each over its limit ("
             + ", ".join(f"{most:g} s in {f if os.path.sep not in f else os.path.basename(os.path.dirname(os.path.dirname(os.path.dirname(f))))}" for f, most in limits) + ")"
             if left else ""), flush=True)
    status = 0
    if run:
        junit = os.path.join(build, "Testing", "gate_cache_junit.xml")
        regex_run = "^(" + "|".join(re.escape(n) for n in run) + ")$"
        # (Stage 68) GATE_TEST_LOAD=n: no entry started while the machine's load is over n. Off unless asked: ctest
        # then starts none at all, its own first among them, while another's run keeps the load up, and a gate
        # waited out another worktree's whole run
        most_load = os.environ.get("GATE_TEST_LOAD", "")
        status = subprocess.run(["ctest", "--test-dir", build, "-j", str(jobs)] + (["--test-load", most_load] if most_load else [])
                                + ["--output-on-failure", "-R", regex_run, "--output-junit", junit]).returncode
        ok = set()
        try:
            text = open(junit).read()
            ok = set(re.findall(r'<testcase name="([^"]+)"[^>]*status="run"', text))
            ok -= set(re.findall(r'<testcase name="([^"]+)"[^>]*status="fail"', text))
        except OSError:
            pass
        def changes(cache):
            for name in run:
                if name in keys:
                    record(cache, entry_of(build, name), keys[name], name in ok)
        update(changes)
        share_costs(build)
    return status


def step(name, inputs, command, use_cache):
    key = hashlib.sha256(json.dumps([fingerprint(inputs), rc_env(), command]).encode()).hexdigest()
    if use_cache and passed(load(), "step:" + name, key):
        print(f"gate_cache: {name} unchanged since it passed", flush=True)
        return 0
    status = subprocess.run(command).returncode
    update(lambda cache: record(cache, "step:" + name, key, status == 0))
    return status


STEP_COSTS = os.path.join(os.path.dirname(CACHE), "step_costs.json")  # (Stage 75)


def git_or_empty(*args):
    try:
        return git(*args)
    except (subprocess.CalledProcessError, OSError):
        return ""


BUILT_FROM = ("native", "data", "bench")  # what the game and its sessions are made of: a document's commit is not


def built_from(commit):
    """The trees the game is built from at a commit, as one hash (a commit that changes only documents keeps it)."""
    trees = [git_or_empty("rev-parse", f"{commit}:{path}").strip() for path in BUILT_FROM]
    return hashlib.sha256(" ".join(trees).encode()).hexdigest() if all(trees) else ""


def clean_tree():
    """What HEAD builds from when it has no edits there, else None."""
    if git_or_empty("status", "--porcelain", "--", *BUILT_FROM).strip():
        return None
    return built_from("HEAD") or None


def main_base():
    """The tree where this branch left main: the merge base's, or on main itself, its first parent's."""
    head = git_or_empty("rev-parse", "HEAD").strip()
    for branch in ("main", "origin/main"):
        base = git_or_empty("merge-base", "HEAD", branch).strip()
        if base:
            if base == head:
                base = git_or_empty("rev-parse", "HEAD^1").strip()
            return base, built_from(base)
    return "", ""


def store(key, value):
    import fcntl
    os.makedirs(os.path.dirname(STEP_COSTS), exist_ok=True)
    with open(CACHE + ".lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            with open(STEP_COSTS) as f:
                costs = json.load(f)
        except (OSError, ValueError):
            costs = {}
        costs[key] = value
        tmp = f"{STEP_COSTS}.{os.getpid()}.tmp"
        with open(tmp, "w") as f:
            json.dump(costs, f, indent=0, sort_keys=True)
        os.replace(tmp, STEP_COSTS)


def stepcost(binary, session, use_cache):
    key = hashlib.sha256(json.dumps([sha_binary(binary), fingerprint(DATA), os.path.normpath(session)]).encode()).hexdigest()
    def read():
        try:
            with open(STEP_COSTS) as f:
                return json.load(f)
        except (OSError, ValueError):
            return {}
    known = read().get(key) if use_cache else None
    if known is None:
        out = subprocess.run([binary, "bench", session], capture_output=True, text=True)
        if out.returncode != 0:
            print(out.stdout + out.stderr, flush=True)
            return out.returncode
        m = re.search(r"step cost: ([0-9.]+) (.+) a step", out.stdout)
        if not m:
            print("gate_cache: the bench printed no step cost", flush=True)
            return 1
        known = {"cost": float(m.group(1)), "unit": m.group(2), "session": os.path.normpath(session), "tree": clean_tree()}
        store(key, known)
    elif known.get("tree") is None and clean_tree():
        known["tree"] = clean_tree()  # measured on edits since committed: the same binary, now a tree's
        store(key, known)
    name = os.path.splitext(os.path.basename(session))[0]
    line = f"step cost: {known['cost']:.0f} {known['unit']} a step ({name})"
    base, tree = main_base()
    against = None
    for v in read().values():
        if tree and v.get("tree") == tree and v.get("session") == known["session"] and v.get("unit") == known["unit"]:
            against = v["cost"]
    if against:
        change = 100.0 * (known["cost"] / against - 1.0)
        line += f", {change:+.1f} % against main's at {base[:7]}"
        print(line, flush=True)
        warn = float(os.environ.get("GATE_STEPCOST_WARN", "10"))
        if change > warn:
            print(f"!! the step costs {change:+.1f} % more than main's at {base[:7]} ({name}): more than {warn:g} %", flush=True)
    else:
        print(line + (f"; none known for main's at {base[:7]} (a gate there gives one)" if base else ""), flush=True)
    return 0


def main(argv):
    use_cache = os.environ.get("GATE_CACHE", "1") != "0"
    if not argv:
        print(__doc__)
        return 2
    if argv[0] == "clear":
        if os.path.exists(CACHE):
            os.remove(CACHE)
        return 0
    if argv[0] == "ctest" and len(argv) >= 2:
        jobs, regex, args = os.cpu_count() or 4, None, argv[2:]
        limits = []
        while args:
            if args[0] == "--jobs":
                jobs, args = int(args[1]), args[2:]
            elif args[0] == "--regex":
                regex, args = args[1], args[2:]
            elif args[0] == "--no-cache":
                use_cache, args = False, args[1:]
            elif args[0] == "--limit":
                f, _, most = args[1].rpartition(":")
                limits.append((f, float(most)))
                args = args[2:]
            else:
                print(f"unknown option {args[0]}")
                return 2
        return ctest(argv[1], jobs, regex, use_cache, limits)
    if argv[0] == "stepcost" and len(argv) == 3:
        return stepcost(argv[1], argv[2], use_cache)
    if argv[0] == "step" and "--" in argv and len(argv) >= 4 and argv[2] == "--inputs":
        sep = argv.index("--")
        return step(argv[1], argv[3:sep], argv[sep + 1:], use_cache)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
