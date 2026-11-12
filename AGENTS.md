# Instructions for the agent

This repo is a **template for a hands-on course**: the learner learns a topic
by implementing it, one tested exercise at a time. You build and maintain the
course; the learner writes the solutions. [PLAYBOOK.md](PLAYBOOK.md) is the
design (read it fully before your first change); this file is how you act.

First, work out which situation you're in:

- `docs/project.md` still has empty answers and `curriculum.json` still
  holds the placeholder exercises → **Bootstrap** (below).
- Otherwise → **Maintain** (below). Read `docs/project.md` and
  `curriculum.json` first.

## Hard rules (always)

- **Never push.** Not to any branch, ever. Only the learner pushes.
- **Never act as the learner** on any remote or website: no PRs, issues,
  comments, nothing.
- **`main`: no commits.** Edit the working tree only; the learner reviews and
  commits. (Exception: during bootstrap, if the learner explicitly asks you to
  commit the scaffold.)
- **`solutions`: you commit, as yourself**, so the learner is never
  impersonated:
  ```sh
  git -C ../<course>-solutions commit --author="<Agent name> <agent email>" -m "..."
  ```
  (Claude: `--author="Claude <noreply@anthropic.com>"`.) Commit only after
  `verify.sh` passes. Never rewrite published history.
- **Never overwrite the learner's work.** On `main`, a file with `TODO(you)`
  markers belongs to the learner once they've touched it (`git diff`, or
  fewer markers than in history). To change a stub (new function, reworded
  hint), edit only the affected part and say so; never reset a file to its
  stub.
- **No spoilers on `main`.** Reference solutions live only on the
  `solutions` branch. When the learner asks for help, give hints in order of
  strength (a question, the relevant invariant, a pointer to the pitfall
  test, pseudo-code) and stop at the weakest one that unblocks them. Show
  solution code only when they explicitly ask for it.
- **Expected values come from the oracle**, never from running the
  reference solution.

## Bootstrap: turn the template into a course

### 1. Interview the learner

Ask, a few questions at a time, and don't proceed on guesses for the first
three:

1. **Topic and end goal.** What should they be able to build or explain at
   the end? Push for something concrete and testable ("a working X that
   does Y on real data").
2. **Background.** What do they already know (the topic, the language,
   adjacent fields)? What do they want to skip?
3. **Language and stack.** Default: what they're fluent in, unless learning
   the language is part of the goal. For C++, propose PLAYBOOK §12.
4. Machine/OS (affects toolchains); interest in real data, benchmarks,
   side tracks.
5. Optional: how long is a typical sitting? It only sets exercise size (one
   exercise should fit in one sitting, so they stop at green tests, not
   mid-thought). If they don't care, size exercises for 1–2 hours. Don't
   ask about time per week: it changes the pace, not the course.

Then propose, in a short message: 2–5 **tracks** (goals) each ending in a ★
milestone, the **levels**, the **domains** (directories), the oracle(s), and
a first slice of 5–10 exercises. Iterate until they agree.

### 2. Write the course skeleton (on `main`, uncommitted)

1. `docs/project.md`: answer every "Project specifics" question.
2. `curriculum.json`: replace the placeholders. List the whole planned
   course (planned exercises are fine, they show faded), with edges and
   presets. Set the `progress` block for the stack (PLAYBOOK §8).
   Run `python3 tools/sync_curriculum.py` until it reports ok.
3. Stack: for C++, copy `starters/cpp-bazel/` into the repo root (merge
   `gitignore.append` into `.gitignore`), rename the module, the
   `course` namespace and the `COURSE_` macro prefix to the course's own,
   and delete `example/` once a real exercise exists. For Python or Rust,
   use `starters/python-uv/` or `starters/rust-cargo/` the same way (each
   has a README with the steps and the `progress` block). For another
   stack, set up the equivalent (PLAYBOOK §11): pinned toolchain, test
   runner, shared helpers in `common/`, and a runner if the `command` one
   doesn't fit (PLAYBOOK §8).
4. `README.md`: replace the template README with the course README
   (PLAYBOOK §9). Keep a short note that the course was made from this
   template.
5. `docs/how_<topic>_works.md`: the narrative "why", a few pages.
6. Remove `starters/` once used, unless the learner wants to keep it.
7. Licensing. Delete `CONTRIBUTING.md` (it's about the template). **Never
   delete or edit `LICENSE`**: the course keeps template code (tools, the
   map, the playbook, starter files), and MIT requires its copyright notice
   to stay with it. The learner's own work (exercises, solutions, docs) may
   carry a license of their choice: ask them. If they pick one other than
   MIT, add it as a separate file (e.g. `LICENSE-COURSE`) and say in the
   README which license covers what. If the C++ starter is used, keep
   `third_party/README.md`: the hedron patch is Apache-2.0.

### 3. Set up the solutions worktree

```sh
git worktree add --orphan -b solutions ../<course>-solutions
cp -R starters/solutions/. ../<course>-solutions/
```

Its default command is `bazel test //...`; for other stacks change the
`VERIFY_CMD` default at the top of `verify.sh` (the starters' READMEs give it). Mention the worktree in the course README.

### 4. Write the first exercises

Write the first 3–5 ready-to-start exercises (checklist below), not the
whole course. Then run `python3 tools/progress.py`, open `docs/map.html`, and
hand over: what was created, how to start (the first exercise and the
commands), and that `main` is uncommitted for them to review.

## Maintain

Typical requests and what to do:

- **"Add the next exercises" / "write X"** → the checklist below.
- **"I'm stuck on X"** → read their code and the failing test output; give
  the weakest hint that unblocks them (see hard rules). If a test or README
  is wrong or unclear, fix it (and the solution, then `verify.sh`).
- **"Review my solution"** → correctness first (cases the tests don't
  cover), then complexity, then style. Suggest a named test for any bug you
  find; add it on request.
- **"Where am I?"** → `python3 tools/progress.py`, summarise, suggest the
  next exercise from "Next up".
- **"Change the plan"** → edit `curriculum.json`, run `sync_curriculum.py`,
  update `docs/project.md` and the README if tracks change.

Keep consistent on every change:

- When an API file or test changes, update the matching solution and run
  `../<course>-solutions/verify.sh` (and the real-data tests when they're
  affected). Every test must pass against the reference; every stub must
  build and fail its tests, quickly.
- Edges reflect what tests and solutions actually call. Re-check them when
  you write an exercise that was only planned.
- `curriculum.json` is the only place the graph lives; never hand-edit the
  generated `docs/curriculum.js`, `docs/roadmap.md`, `docs/dependencies.md`.

### Checklist: adding an exercise

1. Place it: domain, level, tracks, prerequisites (edge kinds + notes),
   what it unlocks. Respect the level rule.
2. On `main`: `README.md` (PLAYBOOK §4 template), the API file with
   contracts, the stub with `TODO(you): <hint>` markers, tests (values from
   the oracle script, one named test per pitfall), optional tests, build
   file.
3. On `solutions`: the reference solution at the same relative path; run
   `verify.sh`; commit there as yourself (never push).
4. On `main`: check that the stub builds and its tests **fail fast** (short
   timeout).
5. `curriculum.json`: add or update the node and edges; add a preset if it's
   a natural target; run `python3 tools/sync_curriculum.py`.
6. Run `python3 tools/progress.py` to make sure it's picked up.
7. Summarise the change for the learner. Don't commit on `main`.
