"""Fails if an official patch changed without its version going up, because then its update never reaches anyone.

Usage: check-patch-versions.py <base commit> <head commit>
"""

import json
import re
import subprocess
import sys


def git(*arguments):
    done = subprocess.run(["git", *arguments], capture_output=True, text=True)
    return done.stdout if done.returncode == 0 else None


def manifest(commit, patch):
    text = git("show", f"{commit}:patches/{patch}/patch.json")
    return json.loads(text) if text else None


def parts(version):
    return tuple(int(part) for part in version.split("."))


base, head = sys.argv[1:3]
changed = set()
for path in git("diff", "--name-only", base, head, "--", "patches/").splitlines():
    if not path.endswith(".md"):
        changed.add(path.split("/")[1])

problems = []
for patch in sorted(changed):
    now = manifest(head, patch)
    if now is None:
        continue  # Removed.
    version = str(now.get("version", ""))
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        problems.append(f"{patch}: version should look like 1.2.3, not {version!r}")
    elif not isinstance(now.get("sdk"), int):
        problems.append(f"{patch}: sdk should be a number")
    else:
        before = manifest(base, patch)
        if before and before.get("version") and parts(version) <= parts(before["version"]):
            problems.append(f"{patch}: changed, but its version is still {version}. Raise it in patch.json")

print("\n".join(problems) or f"Checked {len(changed)} changed patches.")
sys.exit(1 if problems else 0)
