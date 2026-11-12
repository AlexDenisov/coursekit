#!/usr/bin/env python3
"""End-to-end check of one starter: bootstraps a throwaway course from the
template and the starter, then walks the learner's path.

    python3 tests/e2e.py cpp-bazel        # or python-uv, rust-cargo
    python3 tests/e2e.py cpp-bazel --keep # keep the scratch directory for inspection

1. the untouched stub builds, its tests fail, progress says "not started";
2. an uncommitted edit to the stub makes it "started";
3. a syntax error shows as "doesn't compile" (where the runner can tell);
4. the reference solution on an orphan `solutions` worktree passes verify.sh;
5. the solution copied over the stub makes the exercise done, optional part included.

The reference solutions live in tests/starters/<starter>/solutions/, never in
starters/, so they can't end up in a course. tests/starters/<starter>/fixture.json
holds the starter's progress block, verify command and setup commands.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EX = "example/prefix_sums"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 1:
        sys.exit(__doc__)
    starter = args[0]
    keep = "--keep" in sys.argv
    src = os.path.join(ROOT, "starters", starter)
    fix_dir = os.path.join(ROOT, "tests", "starters", starter)
    fixture = json.load(open(os.path.join(fix_dir, "fixture.json")))
    solutions = os.path.join(fix_dir, "solutions")

    scratch = tempfile.mkdtemp(prefix=f"e2e-{starter}-")
    course = os.path.join(scratch, "demo")
    sols = os.path.join(scratch, "demo-solutions")
    step = 0

    def say(msg):
        nonlocal step
        step += 1
        print(f"\n== {step}. {msg}", flush=True)

    def run(cmd, cwd=course, check=True, env=None):
        print(f"$ {cmd}", flush=True)
        r = subprocess.run(cmd, cwd=cwd, shell=True, text=True, capture_output=True,
                           env={**os.environ, **(env or {})})
        if check and r.returncode != 0:
            print(r.stdout[-4000:], r.stderr[-4000:], sep="\n")
            fail(f"command failed (exit {r.returncode}): {cmd}")
        return r

    def fail(msg):
        print(f"\nFAIL [{starter}]: {msg}\n(scratch directory kept: {scratch})", flush=True)
        sys.exit(1)

    def progress():
        r = run("python3 tools/progress.py")
        text = open(os.path.join(course, "docs", "progress.js"), encoding="utf-8").read()
        data = json.loads(text[text.index("{"):text.rindex("}") + 1])
        print(r.stdout)
        return data, r.stdout

    def expect(cond, msg, data=None):
        if not cond:
            fail(msg + (f"\nprogress.js: {json.dumps(data)}" if data is not None else ""))
        print(f"ok: {msg}")

    # ---------- bootstrap a course, the way AGENTS.md describes
    say(f"bootstrap a course from the template + starters/{starter}")
    os.makedirs(course)
    for name in ("tools", "docs", "curriculum.json"):
        s = os.path.join(ROOT, name)
        (shutil.copytree if os.path.isdir(s) else shutil.copy)(s, os.path.join(course, name))
    shutil.copytree(src, course, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("uv.lock", "Cargo.lock", "target", ".venv",
                                                  "bazel-*", "MODULE.bazel.lock"))
    os.remove(os.path.join(course, "README.md"))  # the starter's README is for the agent
    # The template's .gitignore plus the starter's lines, as AGENTS.md describes.
    with open(os.path.join(course, ".gitignore"), "w") as f:
        f.write(open(os.path.join(ROOT, ".gitignore")).read() + "\n"
                + open(os.path.join(src, "gitignore.append")).read())
    os.remove(os.path.join(course, "gitignore.append"))
    cpath = os.path.join(course, "curriculum.json")
    c = json.load(open(cpath))
    c["progress"] = fixture["progress"]
    c["domains"].append({"id": "example", "name": "Example"})
    c["exercises"].insert(0, {"id": EX, "title": "Prefix sums", "level": "basic",
                              "tracks": [c["tracks"][0]["id"]], "about": "the starter's sample"})
    json.dump(c, open(cpath, "w"), indent=2)
    run("python3 tools/sync_curriculum.py")
    for cmd in fixture.get("setup", []):
        run(cmd)
    # Ignore the machine's global excludes, so a local run sees what CI sees.
    run("git init -q && git config core.excludesFile /dev/null && git add -A"
        " && git -c user.name=e2e -c user.email=e2e@example.com commit -qm init")

    # the fixture must cover exactly the files with TODO(you) stubs
    stub_files = sorted(os.path.relpath(os.path.join(d, f), course)
                        for d, _, fs in os.walk(os.path.join(course, EX)) for f in fs
                        if "TODO(you" in open(os.path.join(d, f), errors="replace").read())
    sol_files = sorted(os.path.relpath(os.path.join(d, f), solutions)
                       for d, _, fs in os.walk(solutions) for f in fs)
    expect(stub_files == sol_files, f"reference solutions cover the stub files {stub_files}")
    for f in sol_files:
        expect("TODO(you" not in open(os.path.join(solutions, f)).read(), f"no TODO(you) left in {f}")

    # ---------- 1. untouched stub
    say("untouched stub: builds, tests fail, not started")
    data, _ = progress()
    expect(data["written"] == [EX], "the exercise counts as written", data)
    expect(data["done"] == [] and EX in data["failing"], "the stub's tests fail", data)
    expect(EX not in data["partial"], "the exercise is not started", data)

    # ---------- 2. started
    say("uncommitted edit: started")
    stub = os.path.join(course, fixture["stub"])
    original = open(stub).read()
    open(stub, "w").write(original + "\n")
    data, _ = progress()
    expect(EX in data["partial"], "the exercise is started", data)
    open(stub, "w").write(original)

    # ---------- 3. doesn't compile
    if fixture.get("detects_build_errors"):
        say("syntax error: doesn't compile")
        open(stub, "w").write(original + fixture["break"])
        data, out = progress()
        expect("Doesn't compile" in out and EX in out.split("Doesn't compile")[1].splitlines()[0],
               "reported as doesn't compile", data)
        expect(data["done"] == [], "and not done", data)
        open(stub, "w").write(original)

    # ---------- 4. solutions worktree + verify.sh
    say("reference solution passes verify.sh on an orphan solutions worktree")
    run(f'git worktree add -q --orphan -b solutions "{sols}"')
    shutil.copytree(os.path.join(ROOT, "starters", "solutions"), sols, dirs_exist_ok=True)
    shutil.copytree(solutions, os.path.join(sols, "solutions"), dirs_exist_ok=True)
    vpath = os.path.join(sols, "verify.sh")
    v = open(vpath).read()
    marker = 'DEFAULT_CMD="bazel test //..."'
    expect(marker in v, "verify.sh has a DEFAULT_CMD line to set")
    open(vpath, "w").write(v.replace(marker, f"DEFAULT_CMD={json.dumps(fixture['verify_cmd'])}"))
    r = run("./verify.sh", cwd=sols, check=False)
    print(r.stdout[-3000:], r.stderr[-3000:], sep="\n")
    expect(r.returncode == 0, "verify.sh passes with the reference solution")
    status = run("git status --porcelain").stdout.strip()
    expect(status == "", "verify.sh left main untouched" + (f" (changed: {status})" if status else ""))

    # ---------- 5. solved
    say("learner's solution in place: done, optional part included")
    shutil.copytree(solutions, course, dirs_exist_ok=True)
    data, _ = progress()
    expect(data["done"] == [EX], "the exercise is done", data)
    expect(data["optional"] == [EX], "the optional part is done", data)

    print(f"\nPASS [{starter}]")
    if keep:
        print(f"scratch directory: {scratch}")
    else:
        subprocess.run(["git", "worktree", "remove", "--force", sols], cwd=course, capture_output=True)
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    main()
