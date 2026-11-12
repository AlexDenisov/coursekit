# Starter: Python + uv + pytest

A simple Python setup, ready to copy into the repo root during bootstrap.
This README is for the agent and isn't copied.

```sh
cp -R starters/python-uv/. .            # from the repo root
cat gitignore.append >> .gitignore && rm gitignore.append README.md
```

Then:

1. **Rename** the project in `pyproject.toml`. Bump `.python-version` and
   `requires-python` if the course wants a newer Python; `uv` installs it.
   Then run `uv lock`: the starter ships no lock file (it would be stale),
   and the progress command uses `--frozen`, which needs one. Commit
   `uv.lock` in the course.
2. **Progress.** In `curriculum.json`:
   ```json
   "progress": {
     "runner": "command",
     "test": "uv run --frozen pytest -q {dir} -m \"not optional and not real\" --junitxml=.junit/{name}.xml",
     "optional_test": "uv run --frozen pytest -q {dir} -m optional",
     "junit": ".junit/{name}.xml",
     "timeout": 300
   }
   ```
   For `verify.sh` on the solutions branch: `VERIFY_CMD='uv run --frozen pytest -q'`.
3. **Check it works:**
   ```sh
   uv run pytest common                     # passes
   uv run pytest example/prefix_sums        # runs, fails (it's a stub)
   uv run ruff check . && uv run ruff format --check .
   ```
4. **Delete `example/`** once the first real exercise exists.

Libraries the course needs (numpy for an oracle, say) go in with `uv add`;
commit `uv.lock`. Oracle scripts: `uv run tools/<x>_reference.py`.

## Conventions

- Exercise modules are imported from the repo root:
  `from parsing.lexer.lexer import Lexer`. No `__init__.py` needed in
  exercise directories (pytest runs with `--import-mode=importlib` and
  `pythonpath = ["."]`).
- Files per exercise: `README.md`, `<name>.py` (API with docstring
  contracts and type hints; stubs marked `TODO(you)`), `<name>_test.py`,
  `optional_test.py` (`pytestmark = pytest.mark.optional`),
  `real_<x>_test.py` (`pytest.mark.real`, skip when data is missing).
- Contract violations raise (`IndexError`, `ValueError`) with a message
  that shows the values.
- `pytest-timeout` (30 s) turns a hanging stub into a failure.
- `common/rng.py`: deterministic splitmix64, the same sequence as the C++
  and Rust starters.

Commands the course README should show:

```sh
uv run pytest <domain>/<exercise>                                   # one exercise
uv run pytest <domain>/<exercise> -m "not optional"                 # required part only
uv run pytest <domain>/<exercise> -k large_values                   # tests matching a name
uv run pytest <domain>/<exercise> -x --pdb                          # stop at the first failure, debug
```
