# Contributing

Thanks for helping improve coursekit. This guide is about the **template**:
the playbook, the tooling, the map, the runners and the starters. A course
made from the template is its learner's own project; its content doesn't
come back here.

## What fits

- Fixes and improvements to `tools/`, `docs/map.html`, the starters and the
  runners.
- New **starters** (a language or build system) and new **runners**.
- Playbook changes backed by experience: a lesson learned while building or
  taking a course, stated in general terms.
- Clearer wording in `README.md`, `PLAYBOOK.md`, `AGENTS.md`.

What doesn't: topic-specific exercises or curricula, and dependencies the
template doesn't need. If you're unsure, open an issue first.

## Ground rules

- **Stay generic.** No project names, topics or courses in the playbook,
  tooling or docs. Examples use placeholders (`parsing/lexer`,
  `example/prefix_sums`).
- **Python tools use the standard library only** (Python 3.9+), so a fresh
  clone works with nothing installed but `python3`.
- **The map stays one self-contained file.** No build step; it opens from
  `file://`. Data comes only from `docs/curriculum.js` and
  `docs/progress.js`. Keep light and dark themes, phone width, and keyboard
  access working.
- **`curriculum.json` is the only source of the graph.** Anything that needs
  it reads it or a file generated from it.
- **The runner protocol (PLAYBOOK §8) is a stable interface.** Courses may
  have their own runners. Add optional fields instead of changing existing
  ones, and document changes in the playbook.
- **Starters stay small**: a pinned toolchain, test setup, `common/` helpers,
  and the `example/prefix_sums` sample. They ship no lock files or build
  outputs (see `.gitignore`).
- Keep `AGENTS.md`'s hard rules intact (never push, no commits on `main`,
  never overwrite the learner's work, no spoilers on `main`). Changes there
  need a clear reason.

## Before you open a PR

```sh
python3 tools/sync_curriculum.py --check      # curriculum valid, generated docs up to date
python3 -m py_compile tools/*.py tools/runners/*.py
```

If you touched a starter, a runner, `progress.py` or `verify.sh`, run the
starter end to end in a scratch copy, as its README describes:

1. copy the template and the starter into an empty directory, add
   `example/prefix_sums` to `curriculum.json`, `git init` and commit;
2. untouched stub: it builds, its tests fail quickly, and
   `python3 tools/progress.py` reports it as *not started*;
3. half-solved stub: *in progress* with the right passing/total counts;
4. reference solution on an orphan `solutions` worktree: `verify.sh` passes;
5. a syntax error in the stub: *doesn't compile* (where the runner can tell);
6. the starter's linters/formatters are clean.

If you touched `docs/map.html`, open it with and without `docs/progress.js`,
in light and dark mode, and at phone width.

## Adding a starter

`starters/<language>-<tool>/` with:

- `README.md` for the agent: how to copy it in, what to rename, the
  `progress` block for `curriculum.json`, the `verify.sh` command, how to
  check it works, conventions (files per exercise, how optional and
  real-data tests are marked), and the commands the course README should
  show;
- the build/toolchain files, pinned;
- `common/` with a deterministic RNG producing the same splitmix64 sequence
  as the other starters (seed 0 → `0xE220A8397B1DCDAF`, `0x6E789E6AA1B965F4`),
  and its tests;
- `example/prefix_sums/` matching the other starters: the same API,
  pitfall test, brute-force oracle and optional part;
- `gitignore.append` for the course's `.gitignore`.

Then list it in `README.md`, PLAYBOOK §11 and the bootstrap step in
`AGENTS.md`.

## Adding a runner

`tools/runners/<name>.py`, following the protocol in `tools/progress.py`'s
docstring and PLAYBOOK §8. Standard library only; progress messages on
stderr, results on stdout. Document its config keys in the file's docstring
and in the PLAYBOOK §8 table. Add the name to `BUILTIN` in
`tools/progress.py` only if it's generally useful; otherwise a course can
point `progress.runner` at its own script.

## Commits and PRs

Small, focused PRs with a message that says why. Contributions are accepted
under the repository's [MIT license](LICENSE).
