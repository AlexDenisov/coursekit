#!/usr/bin/env bash
# Runs the course's tests with the reference solutions swapped in, in a private
# copy of the exercises. The exercise checkout (the `main` worktree) is never
# modified, so this is safe while the learner is mid-edit.
#
#   ./verify.sh                    # the default test command (below)
#   ./verify.sh <command ...>      # any command, run inside the private copy
#                                  # e.g. ./verify.sh bazel test //core/combination/...
#
# COURSE_REPO     the exercise checkout   (default: ../<this dir without -solutions>)
# VERIFY_CMD      the default command     (default: DEFAULT_CMD below)
# VERIFY_EXCLUDE  extra rsync excludes, space-separated (build outputs, downloaded data)
set -euo pipefail

# The course's test command; set once at bootstrap (bazel test //... | cargo test -q --workspace |
# uv run --frozen pytest -q | ...). VERIFY_CMD overrides it for one run.
DEFAULT_CMD="bazel test //..."

HERE="$(cd "$(dirname "$0")" && pwd)"
DEFAULT_REPO="$HERE/../$(basename "$HERE" | sed 's/-solutions$//')"
REPO="$(cd "${COURSE_REPO:-$DEFAULT_REPO}" && pwd)"
TREE="$HERE/.verify_tree"   # a stable path keeps incremental builds incremental

excludes=(--exclude .git --exclude 'bazel-*' --exclude /external --exclude .cache
          --exclude compile_commands.json --exclude user.bazelrc --exclude /docs/progress.js
          --exclude node_modules --exclude target --exclude .venv --exclude __pycache__)
for e in ${VERIFY_EXCLUDE:-}; do excludes+=(--exclude "$e"); done

mkdir -p "$TREE"
rsync -a --delete "${excludes[@]}" "$REPO/" "$TREE/"
rsync -a --exclude .gitkeep "$HERE/solutions/" "$TREE/"
# Machine-local settings (e.g. paths to downloaded data) come from the exercise checkout.
if [ -f "$REPO/user.bazelrc" ]; then cp "$REPO/user.bazelrc" "$TREE/user.bazelrc"; fi

cd "$TREE"
if [ $# -eq 0 ]; then
  exec bash -c "${VERIFY_CMD:-$DEFAULT_CMD}"
fi
exec "$@"
