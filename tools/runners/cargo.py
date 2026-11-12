#!/usr/bin/env python3
"""Runner for a cargo workspace with one crate per exercise. See tools/progress.py
for the protocol.

Per exercise: `cargo test -p <package> --no-fail-fast --test <t>` for each required
integration-test target, parsing cargo's "test result: ... N passed; M failed"
lines for the counts; then the optional targets (pass = optional part done).

Keys of the "progress" block in curriculum.json:

  package         package name for an exercise, default "{name}" (the slug);
                  {id}, {dir}, {name} are substituted
  required_tests  integration-test targets (tests/<t>.rs) that are required, default ["required"]
  optional_tests  ... that are optional, default ["optional"]
  timeout         seconds per cargo call, default 600
"""
import json
import re
import subprocess
import sys

req = json.load(sys.stdin)
cfg = req["config"]
required = cfg.get("required_tests", ["required"])
optional = cfg.get("optional_tests", ["optional"])
timeout = cfg.get("timeout", 600)


def cargo_test(pkg, target):
    """(exit code, passed, failed, output)."""
    cmd = ["cargo", "test", "-q", "-p", pkg, "--no-fail-fast", "--test", target, *req.get("args", [])]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return 124, 0, 0, ""
    out = r.stdout + r.stderr
    passed = sum(int(n) for n in re.findall(r"test result: \w+\. (\d+) passed", out))
    failed = sum(int(n) for n in re.findall(r"test result: \w+\. \d+ passed; (\d+) failed", out))
    return r.returncode, passed, failed, out


ids = req["exercises"]
for n, ex in enumerate(ids, 1):
    print(f"  [{n}/{len(ids)}] {ex}", file=sys.stderr, flush=True)
    pkg = cfg.get("package", "{name}").format(id=ex, dir=ex, name=ex.rsplit("/", 1)[-1])
    res = {"id": ex, "status": "pass"}
    ok = total = 0
    for t in required:
        code, p, f, out = cargo_test(pkg, t)
        ok, total = ok + p, total + p + f
        if code != 0:
            # No "test result" line at all: it didn't build (or the target is missing).
            built = "test result:" in out
            res["status"] = "fail" if built and res["status"] != "build" else "build"
    if total:
        res.update(passed=ok, total=total)
    res["optional"] = bool(optional) and all(cargo_test(pkg, t)[0] == 0 for t in optional)
    print(json.dumps(res), flush=True)
