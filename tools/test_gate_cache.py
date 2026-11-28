#!/usr/bin/env python3
"""The result cache's key of the repo's data (tools/gate_cache.py, `fingerprint`; ctest `gate_cache_fingerprint`):
the content's, however git holds it. In a repository of its own: a file edited, the edit staged, the edit
committed are one fingerprint; a new file untracked, added and committed are one; a file deleted, and the deletion
committed, are one; and any change of content, a new file or a file gone is another."""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gate_cache  # noqa: E402

failed = []


def check(what, ok):
    print(("ok: " if ok else "FAIL: ") + what)
    if not ok:
        failed.append(what)


with tempfile.TemporaryDirectory() as root:
    def git(*args):
        subprocess.run(["git", "-C", root, "-c", "user.name=test", "-c", "user.email=test@example.invalid", "-c", "commit.gpgsign=false", *args],
                       check=True, capture_output=True)

    def write(path, text):
        full = os.path.join(root, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w") as f:
            f.write(text)

    def fp():
        return gate_cache.fingerprint(["data", "bench"], root=root)

    git("init", "-q")
    write("data/a.json", "{\"a\": 1}\n")
    write("data/sub/b with a space.json", "{\"b\": 2}\n")
    write("bench/c.txt", "c\n")
    write("other/d.txt", "d\n")
    git("add", "-A")
    git("commit", "-q", "-m", "first")
    first = fp()

    write("data/a.json", "{\"a\": 3}\n")
    edited = fp()
    check("an edit is another fingerprint", edited != first)
    git("add", "data/a.json")
    check("the edit staged: the same", fp() == edited)
    git("commit", "-q", "-m", "edit")
    check("the edit committed: the same", fp() == edited)
    write("data/a.json", "{\"a\": 1}\n")
    check("the content back as it was: the first fingerprint", fp() == first)
    git("commit", "-q", "-am", "back")

    write("data/new.json", "{}\n")
    new = fp()
    check("a new untracked file is another fingerprint", new != first)
    git("add", "data/new.json")
    check("the new file added: the same", fp() == new)
    git("commit", "-q", "-m", "new")
    check("the new file committed: the same", fp() == new)

    os.remove(os.path.join(root, "data/sub/b with a space.json"))
    gone = fp()
    check("a file deleted is another fingerprint", gone != new)
    git("rm", "-q", "--cached", "data/sub/b with a space.json")
    check("the deletion staged: the same", fp() == gone)
    git("commit", "-q", "-m", "gone")
    check("the deletion committed: the same", fp() == gone)

    write("other/d.txt", "changed\n")
    check("a file outside the paths asked changes nothing", fp() == gone)
    write("bench/c.txt", "c\n")
    check("a file written again with the same content changes nothing", fp() == gone)

sys.exit(1 if failed else 0)
