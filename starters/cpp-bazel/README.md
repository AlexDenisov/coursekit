# Starter: C++20 + Bazel + GoogleTest + hermetic clang

The recommended C++ stack from PLAYBOOK §12, ready to copy into the repo
root during bootstrap. This README is for the agent and isn't copied.

```sh
cp -R starters/cpp-bazel/. .            # from the repo root
cat gitignore.append >> .gitignore && rm gitignore.append README.md
```

Then:

1. **Rename** the module in `MODULE.bazel`, the `course` namespace and the
   `COURSE_` macro prefix in `common/` and `example/` to the course's own
   (e.g. `db`/`DB_`).
2. **Platforms.** `MODULE.bazel` fetches clang from
   [mull-project/toolchains](https://github.com/mull-project/toolchains/releases)
   for macOS on Apple silicon (`darwin-aarch64`) and Linux (`linux-x86_64`,
   `linux-aarch64`); CI builds on all three. The archives contain no C++
   standard library: macOS uses libc++ from the SDK (Xcode or the Command
   Line Tools), Linux links the host's libstdc++ statically (install `g++`,
   or `libstdc++-<ver>-dev` on Debian/Ubuntu). Other platforms: add their
   archives from the same release (`urls`, `sha256`, `strip_prefix`).
3. **Progress.** In `curriculum.json`: `"progress": { "runner": "bazel" }`.
   For `verify.sh`, the default `bazel test //...` already fits.
   The first Bazel command writes `MODULE.bazel.lock`; commit it in the course
   (`bazel mod deps` creates it without building anything).
4. **Check it works:**
   ```sh
   bazel test //common/...                            # passes
   bazel test //example/prefix_sums/...               # builds, fails (it's a stub)
   bazel run :refresh_compile_commands                # compile_commands.json for clangd
   ```
5. **Delete `example/`** once the first real exercise exists. It shows the
   shape of an exercise (README, header with contracts, stub with
   `TODO(you)`, tests named by behaviour with a brute-force oracle, an
   optional part).

## What's here

| File | |
|---|---|
| `MODULE.bazel`, `.bazelversion` | bzlmod deps (rules_cc, googletest, toolchains_llvm, hedron), pinned Bazel |
| `.bazelrc` | warnings, `--config=asan`, `--config=opt`, `user.bazelrc` import |
| `.clang-format` | Google style, 100 columns |
| `BUILD.bazel` | `bazel run :refresh_compile_commands` for clangd |
| `third_party/` | hedron patch for Bazel 9 |
| `common/check.h` | `COURSE_CHECK`, `_EQ/_NE/_LT/_LE/_GT/_GE`, `COURSE_ASSERT`, `COURSE_UNREACHABLE` |
| `common/rng.h` | deterministic RNG (splitmix64), same numbers on every platform |
| `example/prefix_sums/` | a sample exercise |
| `gitignore.append` | lines for `.gitignore` |

Commands the course README should show:

```sh
bazel test //<domain>/<exercise>/...                                  # one exercise
bazel test //<domain>/<exercise>:<name>_test                          # required part only
bazel test //<domain>/<exercise>:<name>_test --test_filter='Suite.Fact*'
bazel test //<domain>/<exercise>:<name>_test --test_output=all        # see output of passing tests
bazel test --config=asan //<domain>/<exercise>/...                    # AddressSanitizer + UBSan
bazel run --config=opt //<domain>/<exercise>:bench                    # benchmarks
```
