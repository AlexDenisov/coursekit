#!/usr/bin/env python3
"""Runner for Bazel: one `bazel test --keep_going //...` for everything (fast,
cached), then results per package. See tools/progress.py for the protocol.

An exercise's tests are the test targets in its package (and subpackages).
Tests tagged manual (real data) aren't run.

Keys of the "progress" block in curriculum.json:

  optional_targets  target names that are optional, default ["optional_test"]
  bazel_args        extra flags for `bazel test`
  targets           what to test, default "//..."
"""
import json
import os
import re
import shlex
import subprocess
import sys
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True  # no __pycache__ in the learner's repo
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import junit  # noqa: E402

req = json.load(sys.stdin)
cfg = req["config"]
ids = req["exercises"]
optional_names = set(cfg.get("optional_targets", ["optional_test"]))

cmd = ["bazel", "test", "--keep_going", "--test_summary=short", *cfg.get("bazel_args", []),
       *req.get("args", []), "--", cfg.get("targets", "//...")]
print("running: " + " ".join(shlex.quote(c) for c in cmd), file=sys.stderr, flush=True)
r = subprocess.run(cmd, capture_output=True, text=True)
# 0 = all pass, 1 = some targets failed to build, 3 = some tests failed, 4 = no tests.
if r.returncode not in (0, 1, 3, 4):
    print("\n".join((r.stdout + r.stderr).splitlines()[-20:]), file=sys.stderr)
    sys.exit(f"runners/bazel.py: bazel test failed to run (exit {r.returncode})")


def owner(pkg):
    return next((e for e in ids if pkg == e or pkg.startswith(e + "/")), None)


# Summary lines look like: //parsing/lexer:lexer_test   (cached) PASSED in 2.0s
targets = {}  # ex -> {(pkg, name): status}
for line in (r.stdout + r.stderr).splitlines():
    m = re.match(r"^//([^:\s]+):(\S+)\s+(.*)$", line)
    if not m or not owner(m.group(1)):
        continue
    pkg, name, rest = m.groups()
    st = ("pass" if re.search(r"\bPASSED\b", rest) else
          "build" if "FAILED TO BUILD" in rest or "NO STATUS" in rest else "fail")
    targets.setdefault(owner(pkg), {})[(pkg, name)] = st


def cases(pkg, name):
    """(passed, total, crashed) for one test target, or None."""
    d = os.path.join("bazel-testlogs", pkg, name)
    try:
        root = ET.parse(os.path.join(d, "test.xml")).getroot()
    except (ET.ParseError, OSError):
        return None
    # gtest's own xml has the counts on the root; bazel's stand-in (written when the
    # binary died: abort, segfault) doesn't. Then count from the gtest log.
    if root.get("tests") is not None:
        c = junit.counts([os.path.join(d, "test.xml")])
        return (*c, False) if c else None
    try:
        text = open(os.path.join(d, "test.log"), errors="replace").read()
    except OSError:
        return None
    m = re.search(r"Running (\d+) tests?", text)
    return len(re.findall(r"^\[       OK \]", text, re.M)), int(m.group(1)) if m else 0, True


for ex in ids:
    t = targets.get(ex, {})
    required = {k: v for k, v in t.items() if k[1] not in optional_names}
    res = {"id": ex, "status": "build",
           "optional": any(v == "pass" for k, v in t.items() if k[1] in optional_names)}
    if required:
        sts = set(required.values())
        res["status"] = "pass" if sts == {"pass"} else "build" if "build" in sts else "fail"
        if res["status"] != "pass":
            ok = total = 0
            crashed = False
            for k in required:
                c = cases(*k)
                if c:
                    ok, total, crashed = ok + c[0], total + c[1], crashed or c[2]
            if total:
                res.update(passed=ok, total=total, crashed=crashed)
    print(json.dumps(res), flush=True)
