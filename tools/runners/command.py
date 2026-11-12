#!/usr/bin/env python3
"""Runner for any stack: one shell command per exercise. See tools/progress.py
for the protocol (JSON request on stdin, one JSON line per exercise on stdout).

Keys of the "progress" block in curriculum.json (run from the repo root;
{id}, {dir}, {name} are substituted, e.g. parsing/lexer, parsing/lexer, lexer):

  test           required tests; exit 0 = pass                          (required)
  optional_test  optional tests; exit 0 = optional part done            (may be omitted)
  build          compile only; non-zero = "doesn't compile"             (may be omitted)
  junit          glob of JUnit XML files the test command leaves behind,
                 to count passing test cases                            (may be omitted)
  timeout        seconds per command, default 600
"""
import glob
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import junit  # noqa: E402

req = json.load(sys.stdin)
cfg = req["config"]
if not cfg.get("test"):
    sys.exit('runners/command.py: set "progress.test" in curriculum.json (PLAYBOOK.md §8).')
timeout = cfg.get("timeout", 600)


def fill(template, ex):
    return template.format(id=ex, dir=ex, name=ex.rsplit("/", 1)[-1])


def sh(template, ex):
    try:
        return subprocess.run(fill(template, ex), shell=True, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL, timeout=timeout).returncode
    except subprocess.TimeoutExpired:
        return 124


ids = req["exercises"]
for n, ex in enumerate(ids, 1):
    print(f"  [{n}/{len(ids)}] {ex}", file=sys.stderr, flush=True)
    res = {"id": ex, "status": "fail"}
    if cfg.get("build") and sh(cfg["build"], ex) != 0:
        res["status"] = "build"
    else:
        res["status"] = "pass" if sh(cfg["test"], ex) == 0 else "fail"
        if cfg.get("junit"):
            c = junit.counts(glob.glob(fill(cfg["junit"], ex), recursive=True))
            if c:
                res["passed"], res["total"] = c
        if cfg.get("optional_test"):
            res["optional"] = sh(cfg["optional_test"], ex) == 0
    print(json.dumps(res), flush=True)
