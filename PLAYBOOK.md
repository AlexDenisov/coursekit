# Learning-project playbook

A recipe for "learn a topic by building it": small exercises that build on
each other, checked by tests, with a hidden reference solution and an
interactive map of the course.

This file is topic- and language-agnostic, and it's written for both the
learner and the agent that builds and maintains the course. It stays generic:
a course records its own choices in `docs/project.md` (see the last
section). How the agent should behave, step by step, is in
[AGENTS.md](AGENTS.md).

---

## 1. Principles

1. **Learn by implementing.** Every concept ends in code the learner writes.
   Reading-only material is a short preamble, never the main event.
2. **Progressive and connected.** Later exercises call code from earlier
   ones, so the learner's own work keeps getting used, and the milestones are
   built entirely from it.
3. **Tests are the teacher.** Each exercise has tests that fail until it's
   done. Failure messages explain what went wrong, not just that it did.
4. **Truth comes from outside.** Expected values come from an independent
   oracle (a well-known library, the standard library, a brute-force
   version, a published table), never from the reference solution itself.
5. **The reference solution is hidden but verified.** It lives on a separate
   branch and exists to prove that the tests are right and passable.
6. **Short, concrete prose.** A README explains one idea with small worked
   numbers, then states the task. If it needs more than a couple of screens,
   split the exercise.
7. **Not strictly linear.** A dependency graph says what's really needed, so
   the learner can take shortcuts to what interests them.
8. **Milestones.** Every track ends in something real and satisfying (a
   program that runs on real data, a system that does the thing the topic is
   about). Mark them ★.
9. **Goals over textbook order.** Organise the course around what the
   learner wants to be able to do (tracks), not around a table of contents.

## 2. One repo, two branches (worktrees)

```
<workspace>/
  <course>/              branch `main`: exercises and the learner's work (the learner commits here)
  <course>-solutions/    branch `solutions`: worktree of the same repo; reference solutions
                         (spoilers), committed by the agent
```

`solutions` is an **orphan branch**: no history shared with `main`. It holds
only the solutions overlay and `verify.sh`. Set it up once:

```sh
cd <course>
git worktree add --orphan -b solutions ../<course>-solutions
cp -R starters/solutions/. ../<course>-solutions/
```

Why an orphan rather than a branch of `main` with the stubs filled in:

- no merges: changes to tests and headers on `main` never conflict with
  filled-in stubs; the agent edits the matching solution file directly;
- a stray `git merge solutions` into `main` fails ("refusing to merge
  unrelated histories"), so spoilers can't leak into the learner's work;
- progress tracking reads `main`'s history only, and each worktree has its
  own build output, so builds don't interfere.

Both branches may go to the same remote (spoilers there are fine). **Only the
learner pushes.**

The learner writes their solutions **directly on `main`**, in the stub files.
So `main` holds the learner's own work, and the agent must never overwrite it.

### `main` layout

```
README.md               what you'll build, workflow, the big picture, glossary
AGENTS.md  CLAUDE.md    instructions for the agent
PLAYBOOK.md             this file
curriculum.json         the course: levels, tracks, domains, exercises, edges, presets
<build files>           toolchain, pinned versions (see §11 and §12)
common/                 shared helpers: checks, test utilities, deterministic RNG,
                        fake-data generators
<domain>/<exercise>/    one directory per exercise (§4)
docs/
  map.html              the interactive map (§7)
  curriculum.js         generated from curriculum.json
  roadmap.md            generated: exercise tables by track and level
  dependencies.md       generated: the graph as Mermaid
  progress.js           (git-ignored) written by tools/progress.py
  project.md            this course's answers to "Project specifics"
  how_<topic>_works.md  the long-form "why" story, read once at the start and again mid-way
  future_topics.md      ideas not committed to yet
tools/
  sync_curriculum.py    validates curriculum.json, regenerates the docs above
  progress.py           runs the tests (through a runner), prints where you are, writes docs/progress.js
  runners/              command.py (any stack), bazel.py, cargo.py, junit.py (helper); add your own here
  download_*.sh         fetch real data once (into a git-ignored directory)
  *_reference.py        produce expected values from the oracle
starters/               copy-in starting points (removed or kept as reference after bootstrap)
```

### `solutions` layout

```
README.md      "spoilers" warning + how verify works
verify.sh      overlay solutions onto a private copy of `main` and run the tests
solutions/<domain>/<exercise>/<file>   same relative paths as on `main`;
                                       only the files that contain TODO(you) stubs
.verify_tree/  (git-ignored) the private copy; a stable path keeps builds incremental
```

`verify.sh` copies the `main` worktree (minus `.git`, build outputs and
downloaded data), overlays `solutions/`, and runs the test command there.
The exercise checkout is never modified, so verification is safe while the
learner is mid-edit.

## 3. The curriculum (`curriculum.json`)

The single source of truth for the course. The map, `progress.py` and the
generated docs all read it; nothing else lists exercises or edges. After
every edit run:

```sh
python3 tools/sync_curriculum.py          # validate + regenerate docs/curriculum.js, roadmap.md, dependencies.md
python3 tools/sync_curriculum.py --check  # validate only; fails if generated files are stale (for CI)
```

```jsonc
{
  "title": "Course name",
  "about": "one paragraph",
  "progress": { "runner": "command", "test": "...", ... },      // §8
  "levels":  [{ "id": "basic", "name": "Basic", "meaning": "..." }, ...],   // ordered, easiest first
  "tracks":  [{ "id": "t", "color": "--p1", "name": "...", "goal": "..." }],  // goals; colours --p1..--p10
  "domains": [{ "id": "d", "name": "..." }],                    // top-level directories
  "exercises": [{
    "id": "<domain>/<slug>",           // also its directory
    "title": "...", "level": "basic",
    "tracks": ["t", ...],              // the first is its main track
    "kind": "concept|algo|perf|infra|project|...",  // free-form, for your own grouping
    "about": "one line: what you'll understand",
    "milestone": true,                 // optional: ★
    "given": true                      // optional: implemented already; read, don't write
  }],
  "edges": [["<from>", "<to>", "req|part|real|learn|idea", "note?"]],
  "presets": [{ "label": "...", "targets": ["<id>"], "level?": "...", "mode?": "learn|code", "real?": false }]
}
```

- **Ids are stable slugs, never numbers.** `parsing/tokenizer`, not
  `03_tokenizer`. Numbered directories make insertion painful: inserting
  renames directories and breaks history and cross-references. The order
  lives in the data.
- **Levels are phases.** Rule (enforced): an exercise depends only on
  exercises at its level or below. So "up to intermediate" is always a
  complete course.
- **Tracks are goals** ("build a working X", "understand why Y is fast"). An
  exercise can serve several tracks; the first listed is where it's drawn.
- **Domains are directories**: where the code lives, independent of goals.
- An exercise is **written** once `<id>/README.md` exists. Listing planned
  exercises in advance is encouraged; they show faded on the map.

## 4. Anatomy of an exercise

```
<domain>/<exercise>/
  README.md            idea + task
  <api file>           the API. Signatures are fixed; tests rely on them
  <impl file>          stubs marked TODO(you)
  <tests>              required tests
  optional tests       (some) tests for optional parts
  real-data tests      (some) on real data; slow, opt-in, skipped if data is missing
  bench                (perf exercises)
  fake_<x>             (some) test doubles so this exercise doesn't need a slow earlier one
  <build file>
```

File names depend on the stack; §12 gives the C++ names.

### README template

```markdown
# <id>: <Title>

> **Summary.** Why this exists and where it's used later (by exercise id).
> The one surprising thing it teaches (e.g. "the first place overflow bites").
>
> **Before:** <prereqs>. **Next:** <what this unlocks>.

## <Concept in one or two sentences>
## <The formula / invariant / algorithm>, with a tiny worked example in a code block
## <The pitfall> (numerical, edge case, complexity trap) and its fix
## API
| Function | Types / complexity / contract |
|---|---|
| `f(x)` | ... |
| `g(x)` (optional) | ... and which later exercise needs it |

## Task
    <command that runs the required tests>
    <command that runs the optional tests>
```

Style: second person, present tense, numbers over adjectives, no filler.
Cross-reference other exercises by id. Explain *why* a later exercise needs
this one.

### API file (the spec)

- One namespace/module per course.
- Every function has a comment with the contract: input/output types or
  shapes, complexity, preconditions, and the formula. The API file is the
  spec.
- Optional functions are marked `Optional.` and say who needs them.

### Stub

- Compiles (or imports) and links. Returns a dummy value so the tests
  *fail*; it doesn't break the build.
- Keeps given checks that guard the contract.
- `TODO(you): <one-line hint of the approach>`; `TODO(you, optional)` for
  optional parts. `progress.py` counts these markers to tell that an
  exercise has been started.
- Some exercises are **given** (`"given": true`): fully implemented,
  read-only, because writing them teaches little about the topic (parsers,
  printers, data loaders).

## 5. Tests and oracles

- Named by behaviour: `Softmax.LargeInputsDoNotOverflow`,
  `Interval.EmptyIntersectionIsEmpty`. A test name reads as a fact the
  learner should now know.
- The mix: exact small values from the oracle; properties (sums to 1,
  invariance, idempotence, round trips); edge cases (empty, size 1,
  extremes); and one "the naive approach fails here" test per pitfall.
- Shared comparison helpers print *where* and *by how much* things differ,
  plus the full values.
- Deterministic: fixed seeds through a small RNG of your own (not the
  platform's distributions), so a failing seed fails for everyone. Fake-data
  generators are shared between the tests and the oracle script, so both
  see identical inputs.
- A failed contract check may abort the test binary; the course README says
  so, so a wall of early failures doesn't scare anyone.
- Slow and real-data tests are opt-in (tagged/marked), and *skip*, not fail,
  when the data isn't downloaded. Data paths come from environment
  variables set in a git-ignored local config.
- **Test helpers never call the code under test.** Against a stub, a
  generator that retries until "the input is valid" loops forever, and a
  validator built on the learner's own primitives can approve a wrong
  answer. Give helpers their own small, obviously correct versions.
- **Every stub fails fast.** Run the stub's tests with a short timeout. A
  stub iterator that doesn't advance, or a stub loop condition, turns "tests
  fail" into "tests hang".
- **Oracles are scripts**, checked in under `tools/`, that print or write
  expected values. Write them early: hand-computed expected values rot. When
  a hand-written expectation and the reference solution disagree, suspect
  the expectation first, but check it by hand before changing it.
- **Prefer self-certifying results.** Where the algorithm allows, make the
  answer carry its proof (a witness, a certificate, a primal/dual pair) and
  check the proof in the test. Such tests need no second implementation.

## 6. Dependencies between exercises

Edges come from what the tests and the reference solution **actually call**,
not from build dependencies (build files may link more; an uncalled stub
doesn't matter).

| kind | meaning | drawn as |
|---|---|---|
| `req` | B's tests call A's code; A must work | solid |
| `part` | B needs only one function / A's optional part | dashed |
| `real` | needed only for B's real-data test or bench | dotted |
| `learn` | understand A first, though no code is shared (learning-order view only) | coloured |
| `idea` | helpful background, listed separately | not drawn |

Each edge can carry a short note ("only `argmax()`", "the bench uses the
timer from X"). To cut a slow chain, give later exercises a `fake_<x>`
instead of requiring the real thing.

## 7. The map (`docs/map.html`)

A single self-contained HTML file, opened locally. It's the part that makes
the course feel like a course. It reads `docs/curriculum.js` and, when it
exists, `docs/progress.js`; it never needs editing per course.

- A build graph read left to right: columns = depth of prerequisites; lanes
  by track or by domain (switchable), coloured by track.
- Click a node: everything it needs is highlighted, and the panel below lists
  it **in a workable order**, with edge notes, plus "helpful background" and
  "opens up".
- Node states: done (green ✓), ready now (dashed orange: all prerequisites
  done), needed, target, planned (faded), the rest dimmed.
- Modes: "Learning order" (includes `learn` edges) vs "Code dependencies
  only" (the shortest path to passing tests). Level filter ("up to …").
  Presets. Real-data toggle when `real` edges exist.
- Zoom (buttons, Ctrl/⌘ + wheel, pinch, `+ − 0`), drag to pan.
- No selection: "Start here" with counts and the "Ready now" list.
- Progress: from `progress.js` (test results, read-only, with a timestamp);
  without it, the learner ticks exercises by hand (saved in the browser).
- Light and dark themes; works at phone width; respects reduced motion.

## 8. Progress (`tools/progress.py`)

```sh
python3 tools/progress.py
```

- Runs the tests of every written exercise, tolerating build and test
  failures (they're expected while learning).
- **Done** = all required tests pass. Optional tests are tracked separately;
  real-data tests aren't run.
- **Started** = a file in the exercise has uncommitted changes, or has fewer
  `TODO(you` markers than it ever had in git history.
- Prints an overall bar, one row of cells per track (█ done, ▒ started,
  · not started), "In progress" with passing/total test cases, "Doesn't
  compile", "Next up" per track with missing prerequisites, "Also ready".
- Writes `docs/progress.js`; the map picks it up on reload.

`progress.py` knows nothing about build systems. Running the tests is the
job of a **runner**, set in `curriculum.json` → `progress.runner`; the rest
of the `progress` block is passed to it as configuration.

| `runner` | what it does |
|---|---|
| `"command"` | `tools/runners/command.py`: one shell command per exercise. Works with any stack |
| `"bazel"` | `tools/runners/bazel.py`: one `bazel test --keep_going //...`, results per package |
| `"cargo"` | `tools/runners/cargo.py`: a cargo workspace, one crate per exercise; counts from cargo's output |
| any other string | a shell command: your own runner, in any language |

**The runner protocol.** The runner reads one JSON object on stdin:
`{"exercises": [ids of the written exercises], "config": {the progress
block}, "args": [what followed -- on progress.py's command line]}`. It prints
one JSON object per line on stdout, one per exercise:

```json
{"id": "parsing/lexer", "status": "fail", "optional": false, "passed": 3, "total": 5, "crashed": false}
```

`status` is `pass` (all required tests pass), `fail`, or `build` (doesn't
compile); the other fields may be omitted. Progress messages go to stderr.
Exit non-zero only if the tests couldn't be run at all. Write your own runner
when the per-exercise command is too slow (one build-system call for
everything is usually much faster) or when your test tool reports results in
its own format. `tools/runners/junit.py` counts test cases in JUnit XML,
which most test frameworks can write.

Keys for the built-in runners:

| key | runner | meaning |
|---|---|---|
| `test` | command | required tests of one exercise; exit 0 = pass. `{id}`, `{dir}`, `{name}` are substituted |
| `optional_test` | command | optional tests; exit 0 = optional part done |
| `build` | command | compile only; non-zero = "doesn't compile" |
| `junit` | command | glob of the JUnit XML that `test` leaves behind, to count passing test cases |
| `timeout` | command | seconds per command (default 600) |
| `optional_targets` | bazel | target names that are optional (default `["optional_test"]`) |
| `bazel_args` | bazel | extra flags for `bazel test` |
| `targets` | bazel | what to test (default `//...`) |
| `package` | cargo | package name of an exercise (default `"{name}"`) |
| `required_tests` / `optional_tests` | cargo | integration-test targets (default `["required"]` / `["optional"]`) |
| `start_marker` | all | the stub marker, default `"TODO(you"` |

Examples:

```jsonc
{ "runner": "bazel" }                                                       // C++ (§12)
{ "runner": "command",
  "test": "uv run pytest -q {dir} -m 'not optional and not real' --junitxml=.junit/{name}.xml",
  "optional_test": "uv run pytest -q {dir} -m optional",
  "junit": ".junit/{name}.xml", "timeout": 300 }                             // Python
{ "runner": "cargo" }                                                       // Rust workspace
{ "runner": "command", "test": "go test ./{dir}/..." }                      // Go
{ "runner": "python3 tools/runners/ctest.py", "build_dir": "build" }       // your own runner
```

## 9. Docs beyond the exercises

- **README**: what you'll build; the workflow with copy-pasteable commands
  (one exercise, one test, one test case, sanitizers, verbose output); data
  downloads; editor setup; "the big picture" (the whole pipeline in about
  six numbered steps, with exercise ids); a link to the map and the
  generated roadmap; a glossary of the terms the learner came in with.
- **how_<topic>_works.md**: the narrative "why", read twice.
- **project.md**: the course's own choices (last section).
- **future_topics.md**: room to grow without clutter.

## 10. Lessons learned

- **One data file, many views.** Keep the graph in `curriculum.json` only;
  generate everything else. Parsing a graph back out of HTML or markdown is
  fragile.
- **Milestones test earlier exercises.** Running several implementations of
  one interface side by side on the same workload finds bugs their own tests
  missed. Turn each such bug into a named test in the exercise it came from
  (edge positions such as "insert at the very front" are a common gap).
- **Search for counterexamples, don't invent them.** When a test needs an
  input where heuristic A loses to optimum B, run a throwaway random search
  and paste the smallest case found.
- **Benchmarks must isolate the thing measured.** If a shared step
  dominates (the same slow helper in every variant), add a phase where only
  the variants differ.
- **Give every pitfall its own named test.**
- **Don't scaffold everything up front.** Write a few exercises, let the
  learner start, and grow the course ahead of them. Infrastructure that the
  learner could build should be an exercise, not a given.

## 11. Choosing a stack

Any language works if it gives you:

- a test runner with named tests, filtering by name, and a non-zero exit
  code on failure (ideally JUnit XML output, for test-case counts);
- a pinned, reproducible toolchain (lock files, hermetic compilers), so the
  course builds the same way next year;
- a way to mark tests optional and slow/real-data;
- sanitizers or a debug mode for memory and undefined-behaviour bugs, where
  the language has them;
- a formatter and a language server.

| Language | Suggested |
|---|---|
| C++ | §12; starter `starters/cpp-bazel/` |
| Python | `uv` (pinned Python, `uv.lock`), `pytest` with markers `optional` / `real`, `pytest-timeout`, `ruff`; starter `starters/python-uv/` |
| Rust | `cargo` workspace, one crate per exercise, `rust-toolchain.toml`, `cargo test`, `clippy`, the `cargo` runner; starter `starters/rust-cargo/` |
| Go | a module, one package per exercise, `go test`, build tags for slow tests |
| Other | whatever gives the list above; record it in `docs/project.md` |

Swapping the language means replacing the file names in §4 and the
`progress` block in §8 (and maybe writing a runner); everything else carries over.

## 12. Recommended stack for C++

A starter with all of this is in [`starters/cpp-bazel/`](starters/cpp-bazel/).

- **Modern C++**: C++20 (or later): concepts, `std::span`,
  `std::string_view`, ranges where they help. Value types and RAII; no raw
  `new`/`delete` in exercise APIs unless memory management is the topic.
- **Bazel** with bzlmod (`MODULE.bazel`), version pinned in `.bazelversion`
  (use `bazelisk`). One package per exercise: `cc_library <name>`,
  `cc_test <name>_test`, `cc_test optional_test`, `cc_binary bench`.
- **Hermetic clang** from
  [mull-project/toolchains](https://github.com/mull-project/toolchains/releases)
  through `toolchains_llvm`, so the host compiler is never used and every
  machine builds with the same clang. The starter wires up macOS on Apple
  silicon (libc++ from the macOS SDK) and Linux x86_64/aarch64 (the host's
  libstdc++, linked statically); add the other platforms you need from the
  same release. `cxx_standard = {"": "c++20"}`.
- **GoogleTest** (`@googletest//:gtest_main`): `TEST(Suite, FactInCamelCase)`,
  `--test_filter`, JUnit XML for free.
- **`.bazelrc`**: `-Wall -Wextra -Wno-unused-parameter` (so stubs compile
  cleanly), `test --test_output=errors`, `--config=asan` (AddressSanitizer +
  UBSan), `--config=opt` for benchmarks, `try-import user.bazelrc` for
  machine-local paths.
- **clangd** through hedron's `refresh_compile_commands` (patched for Bazel
  9 in `third_party/`). The toolchain's own clangd/clang-format may be
  stubs; use Xcode's or Homebrew's.
- **clang-format**: Google style, 100 columns.
- **Checks**: a `<PREFIX>_CHECK` / `_CHECK_EQ` family that prints both sides
  and aborts (`common/check.h`), `_ASSERT` that compiles out under
  `--config=opt`, `_UNREACHABLE`.
- **Determinism**: `common/rng.h` (splitmix64 + unbiased bounded ints), never
  `std::` distributions in tests.
- **Oracles** in Python, run with `uv run tools/<x>_reference.py`; no global
  installs.
- **Progress**: `"progress": { "runner": "bazel" }`. Real-data tests:
  `tags = ["manual"]`, run with `--config=opt`.

Files per exercise: `README.md`, `<name>.h` (API), `<name>.cc` (stubs),
`<name>_test.cc`, `optional_test.cc`, `real_<x>_test.cc`, `bench.cc`,
`fake_<x>.h`, `BUILD.bazel`.

---

## Project specifics (answered per course)

Don't fill these in here: this file stays generic. Answer them in
`docs/project.md`.

- **Topic / end goal:**
- **Learner:** background, what they already know, typical session length (optional)
- **Tracks (goals) and milestones (★):**
- **Levels** (if not basic / intermediate / advanced):
- **Language, toolchain, and deviations from §11–12:**
- **Namespace / check macro prefix:**
- **Oracle(s):**
- **Real data (download scripts, env vars):**
- **Domains (directories):**
- **Given vs built by the learner:**
