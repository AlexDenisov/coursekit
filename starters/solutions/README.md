# Reference solutions (spoilers!)

The `solutions` branch of this course: reference solutions for the exercises
on `main`. They exist to prove that every exercise's tests are correct and
passable, not to read while you do the exercises.

This branch is an orphan (no history shared with `main`) and is checked out as
a worktree next to the exercises:

```
<workspace>/
  <course>/              main: the exercises and your work
  <course>-solutions/    this branch
```

- `solutions/<exercise>/<file>`: same relative paths as on `main`; only the
  files that contain `TODO(you)` stubs on `main`.
- `verify.sh`: copies the `main` worktree to `.verify_tree/` (git-ignored),
  overlays the solutions and runs the tests there.

```sh
./verify.sh                                        # all regular tests
./verify.sh bazel test //core/combination/...      # any command, inside the copy
VERIFY_CMD='cargo test' ./verify.sh                # another build system
```

Commits on this branch are made by the agent, under its own name; only the
learner pushes.
