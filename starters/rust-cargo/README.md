# Starter: Rust + cargo

A simple Rust setup, ready to copy into the repo root during bootstrap. This
README is for the agent and isn't copied.

```sh
cp -R starters/rust-cargo/. .           # from the repo root
cat gitignore.append >> .gitignore && rm gitignore.append README.md
```

Then:

1. **Toolchain.** `rust-toolchain.toml` pins the Rust version; `rustup`
   installs it on first use. Bump it deliberately, not by accident.
   The first `cargo` command writes `Cargo.lock`; commit it in the course.
2. **Progress.** In `curriculum.json`: `"progress": { "runner": "cargo" }`
   (`tools/runners/cargo.py`: counts passing tests from cargo's output).
   For `verify.sh` on the solutions branch:
   `VERIFY_CMD='cargo test -q --workspace'`.
3. **Check it works:**
   ```sh
   cargo test -p common                                # passes
   cargo test -p prefix_sums --test required           # builds, fails (it's a stub)
   cargo clippy --workspace --all-targets && cargo fmt --check
   ```
4. **Delete `example/`** (and its line in `Cargo.toml`'s `members`) once the
   first real exercise exists.

## Conventions

- One crate per exercise, at `<domain>/<exercise>/`, listed in the
  workspace `members`. The package name is the exercise slug, so slugs must
  be unique across domains (or set `"package"` in the progress block).
- Files per exercise: `README.md`, `Cargo.toml`, `src/lib.rs` (API with doc
  comments as contracts; stubs marked `TODO(you)`), `tests/required.rs`,
  `tests/optional.rs`, `tests/real_<x>.rs` (`#[ignore]`, run with
  `--ignored`), `benches/` for perf exercises.
- Using an earlier exercise: a path dependency
  (`lexer = { path = "../../parsing/lexer" }`).
- Contract violations `panic!`/`assert!` with the values in the message.
- Overflow checks stay on in dev builds, so overflow pitfalls fail loudly.
- Workspace lints allow unused variables and dead code, so stubs build
  quietly.
- `common::Rng`: deterministic splitmix64, the same sequence as the C++ and
  Python starters.

Commands the course README should show:

```sh
cargo test -p <exercise>                                  # one exercise
cargo test -p <exercise> --test required                  # required part only
cargo test -p <exercise> --test required overflow         # tests whose name contains "overflow"
cargo test -p <exercise> -- --nocapture                   # see println! output
cargo test -p <exercise> --release -- --ignored           # slow real-data tests
```
