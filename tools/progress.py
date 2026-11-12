#!/usr/bin/env python3
"""Runs every written exercise's tests, prints where you are, and writes
docs/progress.js, which docs/map.html reads to mark finished exercises.

    python3 tools/progress.py              # run the tests, print the report
    python3 tools/progress.py -- <args>    # extra arguments, passed on to the runner

This file knows nothing about build systems. Running the tests is the job of a
*runner*, set by "progress.runner" in curriculum.json:

    "command"      tools/runners/command.py: one shell command per exercise (any stack)
    "bazel"        tools/runners/bazel.py: one `bazel test //...`
    "cargo"        tools/runners/cargo.py: cargo workspace, one crate per exercise
    anything else  a shell command, run from the repo root: your own runner

The runner protocol, so a runner can be written in any language:

  stdin   one JSON object:
            {"exercises": ["<id>", ...],        the written exercises, in course order
             "config": {...},                   the whole "progress" block
             "args": [...]}                     what followed `--` on our command line
  stdout  one JSON object per line, one per exercise:
            {"id": "<id>",
             "status": "pass" | "fail" | "build",   pass = all required tests pass;
                                                    build = doesn't compile
             "optional": true,                  optional tests pass        (may be omitted)
             "passed": 3, "total": 5,           test-case counts           (may be omitted)
             "crashed": true}                   the test binary died early (may be omitted)
  stderr  progress messages for the learner
  exit    0; non-zero only if the tests couldn't be run at all

Exercises the runner says nothing about count as failing.

Common keys of the "progress" block: "start_marker" (default "TODO(you"): an
exercise counts as started when a file in it has uncommitted changes, or has
fewer markers than it ever had in git history.

An exercise is "written" when its directory has a README.md. Exercises listed in
curriculum.json without one are "planned" and shown faded on the map.
"""
import datetime
import glob
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

C = json.load(open("curriculum.json", encoding="utf-8"))
P = {"runner": "command", "start_marker": "TODO(you", **C.get("progress", {})}
EX = {e["id"]: e for e in C["exercises"]}
ORDER = [e["id"] for e in C["exercises"]]
REQUIRES = {}
for a, b, kind, *_ in C["edges"]:
    if kind in ("req", "part"):
        REQUIRES.setdefault(b, set()).add(a)
WRITTEN = [i for i in ORDER if os.path.exists(os.path.join(i, "README.md"))]
EXTRA = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
BUILTIN = {"command", "bazel", "cargo"}


def run_tests():
    """Calls the runner; returns {id: result}."""
    runner = P["runner"]
    if runner in BUILTIN:
        runner = f'"{sys.executable}" tools/runners/{runner}.py'
    request = json.dumps({"exercises": WRITTEN, "config": P, "args": EXTRA})
    r = subprocess.run(runner, shell=True, input=request, stdout=subprocess.PIPE, text=True)
    if r.returncode != 0:
        sys.exit(f"progress.py: the runner failed (exit {r.returncode}): {runner}")
    results = {}
    for line in r.stdout.splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            res = json.loads(line)
        except json.JSONDecodeError:
            print(f"progress.py: ignoring a line the runner printed: {line[:80]}", file=sys.stderr)
            continue
        if res.get("id") in WRITTEN and res.get("status") in ("pass", "fail", "build"):
            results[res["id"]] = res
    for ex in WRITTEN:
        results.setdefault(ex, {"id": ex, "status": "fail"})
    return results


# ---------- started?
def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True)


IN_GIT = git("rev-parse", "--is-inside-work-tree").returncode == 0


def started(ex):
    marker = P["start_marker"]
    for f in sorted(glob.glob(f"{ex}/**/*", recursive=True)):
        if not os.path.isfile(f) or os.path.basename(f) == "README.md":
            continue
        try:
            now = open(f, errors="replace").read().count(marker)
        except OSError:
            continue
        history = git("log", "--format=%H", "--", f).stdout.split() if IN_GIT else []
        counts = [git("show", f"{sha}:{f}").stdout.count(marker) for sha in history]
        most = max(counts + [now])
        if most == 0:
            continue  # not a file you edit
        if history and git("diff", "--quiet", "HEAD", "--", f).returncode == 1:
            return True
        if now < most:
            return True
    return False


# ---------- report
def main():
    if not WRITTEN:
        results = {}
    else:
        print(f"running the tests of {len(WRITTEN)} exercise(s) ...", file=sys.stderr, flush=True)
        results = run_tests()

    done = [i for i in WRITTEN if results[i]["status"] == "pass"]
    optional = [i for i in WRITTEN if results[i].get("optional")]
    failing = [i for i in WRITTEN if i not in done]
    broken = [i for i in WRITTEN if results[i]["status"] == "build"]
    partial = {}
    for i in failing:
        if started(i):
            r = results[i]
            partial[i] = (r.get("passed", 0), r.get("total", 0), bool(r.get("crashed")))

    tty = sys.stdout.isatty() and os.environ.get("NO_COLOR") is None
    color = lambda code, s: f"\033[{code}m{s}\033[0m" if tty else s
    green, yellow, dim, bold = (lambda s: color("32", s)), (lambda s: color("33", s)), \
        (lambda s: color("2", s)), (lambda s: color("1", s))

    def bar(n, total, width=28):
        filled = round(width * n / total) if total else 0
        return green("█" * filled) + dim("░" * (width - filled))

    letter = {l["id"]: l["name"][0].upper() for l in C["levels"]}
    primary = lambda i: EX[i]["tracks"][0]
    print()
    pct = 100 * len(done) // len(WRITTEN) if WRITTEN else 0
    print(bold(f"{len(done)} of {len(WRITTEN)} written exercises done") + f"  {bar(len(done), len(WRITTEN))}  {pct}%   "
          + dim(f"({len(WRITTEN)} of {len(ORDER)} in curriculum.json written)"))
    print()
    width = max([len(t["name"]) for t in C["tracks"]] + [10]) + 2
    for t in C["tracks"]:
        ids = [i for i in ORDER if primary(i) == t["id"]]
        w = [i for i in ids if i in WRITTEN]
        cells = "".join(green("█") if i in done else yellow("▒") if i in partial else dim("·") for i in w)
        k = sum(i in done for i in ids)
        mark = green(" ✓") if w and k == len(w) else ""
        print(f"  {t['name']:<{width}} {cells or dim('nothing written yet')}  {k}/{len(w)}{mark}"
              + dim(f"  (+{len(ids) - len(w)} planned)" if len(ids) > len(w) else ""))
    print(dim("  ") + green("█") + dim(" done  ") + yellow("▒") + dim(" started  · not started"))

    if partial:
        print()
        print(bold("In progress"))
        for ex, (ok, total, crashed) in partial.items():
            note = dim("  (crashes after that: a failed check or a bad index?)") if crashed else ""
            count = f"{ok}/{total} tests pass" if total else "started"
            print(f"  {ex:<36} {count}{note}")
    if broken:
        print()
        print(bold("Doesn't compile: ") + ", ".join(broken))

    todo = [i for i in WRITTEN if i not in done]
    ready = lambda i: REQUIRES.get(i, set()) <= set(done)
    nexts = []
    for t in C["tracks"]:
        ids = [i for i in WRITTEN if primary(i) == t["id"]]
        if any(i in done or i in partial for i in ids):
            nxt = next((i for i in ids if i in partial), None) or next((i for i in ids if i not in done and ready(i)), None)
            if nxt and nxt not in dict(nexts):
                nexts.append((nxt, t["name"]))
    if todo and not nexts:
        first = next((i for i in todo if ready(i)), todo[0])
        nexts.append((first, next(t["name"] for t in C["tracks"] if t["id"] == primary(first))))
    if nexts:
        print()
        print(bold("Next up"))
        for i, name in nexts:
            info = "in progress" if i in partial else "not started"
            missing = sorted(REQUIRES.get(i, set()) - set(done))
            warn = yellow("  needs " + ", ".join(missing)) if missing else ""
            print(f"  {letter[EX[i]['level']]} {i:<36} {dim(name + ', ' + info)}{warn}")
        others = [i for i in todo if ready(i) and i not in dict(nexts)]
        if others:
            print(dim("Also ready (all prerequisites done): " + ", ".join(others[:8])
                      + (f", +{len(others) - 8} more" if len(others) > 8 else "")))
    if not WRITTEN:
        print(dim("No exercise is written yet (an exercise is written once its directory has a README.md)."))
    print()

    js = {"done": done, "optional": optional, "failing": failing, "written": WRITTEN,
          "partial": {i: [p[0], p[1]] for i, p in partial.items()},
          "updated": datetime.datetime.now().strftime("%Y-%m-%d %H:%M")}
    os.makedirs("docs", exist_ok=True)
    with open("docs/progress.js", "w", encoding="utf-8") as f:
        f.write("// Written by tools/progress.py; don't edit. Read by docs/map.html.\n")
        f.write("window.PROGRESS = " + json.dumps(js, indent=1) + ";\n")
    print(dim("Wrote docs/progress.js; reload docs/map.html to see it on the map."))


if __name__ == "__main__":
    main()
