# Integration Test Suite (10.4)

End-to-end shell-script tests that exercise splash through its real
stdin/stdout interface, comparing observed output against expected output.

## Goal

Each integration test pipes a script into the `splash` binary, captures
stdout (and sometimes stderr), and asserts the result string-by-string.
Together with the C unit tests, the integration suite is the primary way
of catching regressions when changing the parser, expander, executor, or
any builtin.

## Layout

```
tests/integration/
├── test_m1_basics.sh          # argv, exit codes, multi-stage pipes
├── test_m2_redirections.sh    # >, >>, <, 2>, >&, >>&
├── test_m3_signals_jobs.sh    # SIGINT/SIGCHLD/jobs/fg/bg
├── test_m4_builtins.sh        # cd, setenv, alias, source, history, ...
├── test_m5_command_subst.sh   # $(...) expansion
├── test_m5_quoting_expansion.sh
├── test_m5_wildcarding.sh
├── test_m7_filters.sh         # where / sort / select / first / count
├── test_m7_structured_*.sh    # ls, ps, find, env structured builtins
├── test_m7_where.sh
├── test_m8_arith.sh           # $(( ))
├── test_m8_brace_group.sh     # { ...; }
├── test_m8_case.sh            # case/esac
├── test_m8_command_lists.sh   # ; && || + if/elif/else + for
├── test_m8_functions.sh       # functions, local, return
├── test_m8_heredoc.sh         # << and <<-
├── test_m8_subshell.sh        # ( ... )
├── test_m8_while.sh           # while / until
└── test_m9_config.sh          # init.sh sourcing, ~/.shellrc compat
```

## Test format

Each script is a self-contained Bash file with the same skeleton:

```bash
#!/bin/bash
SHELL_BIN="./splash"
PASS=0
FAIL=0

assert_eq() {
    if [ "$2" = "$3" ]; then
        echo "  PASS: $1"; PASS=$((PASS + 1))
    else
        echo "  FAIL: $1"
        echo "    expected: $(printf '%q' "$2")"
        echo "    actual:   $(printf '%q' "$3")"
        FAIL=$((FAIL + 1))
    fi
}

OUT=$(echo 'echo hello' | $SHELL_BIN 2>/dev/null)
assert_eq "echo single word" "hello" "$OUT"

# ... more cases ...

echo "=== Results: $PASS passed, $FAIL failed ==="
exit $FAIL
```

The script exits with status equal to the failure count, so a wrapper
runner can detect failures. Some scripts also define `assert_contains`
and `assert_not_contains` helpers for substring checks.

## Running

The Makefile provides three test targets:

```
make test               # C unit tests only (build/test_*)
make integration-test   # all tests/integration/test_*.sh against ./splash
make test-all           # both of the above
```

`make integration-test` iterates the sorted list of `test_*.sh` files,
runs each via `bash`, and aggregates pass/fail counts. Any non-zero exit
status from a script is counted as a failure and the target ultimately
exits non-zero so it can be wired into CI later.

To run the same scripts under the AddressSanitizer + UBSan build:

```
make debug
sed 's|SHELL_BIN="./splash"|SHELL_BIN="./splash-debug"|' \
    tests/integration/test_m1_basics.sh > /tmp/_t.sh && bash /tmp/_t.sh
```

(There is no dedicated Makefile target for this — sanitizer runs are
ad-hoc verification.)

## Why integration tests use non-interactive mode

splash auto-detects whether stdin is a tty (`isatty(0)`) and switches
behavior accordingly. Integration tests pipe input, so splash runs in
**non-interactive** mode, which means:

- The line editor is **not** initialized (no raw mode, no autosuggestions,
  no syntax highlighting in the test path).
- Auto-sourcing of `~/.config/splash/init.sh` and `~/.shellrc` is **skipped**.
- `config_init()` and `config_load()` are **skipped** — see below.
- `history_init()` runs in non-persistent mode — see below.

This is why some interactive features (line editor keystrokes, prompt
rendering, `ON_ERROR` env-var hook, XDG directory creation on first run)
cannot be tested through this harness and are marked as
`(interactive-only feature — manual verification needed)` in the relevant
script. Unit tests pick up the slack on the underlying logic where
possible.

## Test isolation

Two pieces of state could leak between integration runs and were
explicitly fixed as part of 10.4:

### Config directory creation (was 10.4 fix #1)

Before 10.4, `config_init()` always called `mkdir ~/.config/splash` on
startup, regardless of whether splash was interactive. Tests that
override `HOME=/test/home` to verify `$HOME` expansion would trigger a
spurious `splash: mkdir '/test/home/.config': No such file or directory`
on stderr; that error message contaminated the captured output and
broke 9 assertions in `test_m5_quoting_expansion.sh`.

Fix: `config_init()` and `config_load()` are now called only when
`isatty(0)` is true. Non-interactive splash (scripts, tests, pipes)
deliberately does **not** create files in `$HOME` or
`$XDG_CONFIG_HOME`. The interactive-mode behavior is unchanged.

### History persistence (was 10.4 fix #2)

Before 10.4, `history_init()` always loaded
`~/.config/splash/history` from disk. Non-interactive test runs
inherited hundreds of entries from prior interactive sessions, so the
history-dedup test in `test_m4_builtins.sh` failed because the entries
it had just added were not at line 1.

Fix: `history_init()` now takes an `int interactive` parameter. In
non-interactive mode the in-memory history still works (so the
`history` builtin behaves correctly within a session), but no entries
are loaded from disk and `history_add()` does not append to the file.
Tests therefore start with an empty history every run.

## Known limitations

Features that the integration suite **does not** cover (and why):

- **Line editor (M6)**: requires a real tty and raw-mode keystroke
  injection. Manual verification only. Unit tests cover the underlying
  highlight/complete/history-search logic.
- **Variable prompt (9.5)** and **`ON_ERROR` hook (9.6)**: gated on
  `interactive`, not exercised by piped input.
- **XDG directory creation (9.1)**: gated on `interactive` for the
  isolation reason described above.
- **Structured pipes into structured filters that need a tty for
  output formatting**: most are tested, but column-width edge cases
  are simpler to verify in unit tests against `Table`.
- **Latent splash bugs found while writing 10.4 tests** (not yet
  scheduled for fix; intentionally not asserted by tests):
  - Inside `( cmd1; cmd2 )`, `setenv`/`cd` in `cmd1` are not visible
    to `cmd2` — each `;`-separated statement appears to run with a
    fresh state. Workaround: one statement per subshell.
  - `( cmds )` is a parse error if separated by newlines; only `;`
    works inside subshell groups.
  - `true; ( false ); echo $?` prints `0` (should be `1`). The exit
    code propagates correctly when the subshell is not embedded in
    a `;` list.
  - `setenv x val; case $x in val) ... esac` on one line fails: the
    case subject is captured before the `setenv` runs. Workaround:
    separate with newline.
  - `{ cmds; } > file` and `( cmds ) > file` are parse errors;
    redirection on group commands is not yet supported.

## Builtin redirection (10.4 fix #3)

Independently of the integration suite, 10.4 surfaced a bug where
parent-process builtins ignored their redirections:

- The single-command structured-builtin path in `executor.c` only
  applied redirections for `from-csv` / `from-json` / `from-lines`
  sources, so `ls /missing 2> err.txt`, `ls > out.txt`, `ps > out.txt`
  etc. all bypassed the redirect entirely.
- The plain-builtin path called `builtin_execute(cmd)` with no
  redirection handling at all, so `printenv > file`, `history > file`,
  `export > file` were silently broken too.

Both paths now use a small helper pair:

```c
static int  apply_parent_redirections(SimpleCommand *cmd, int saved[3]);
static void restore_parent_stdio(int saved[3]);
```

`apply_parent_redirections()` `dup`s the current stdin/stdout/stderr
into `saved[]` and then runs the same `apply_redirections()` used by
forked external commands. `restore_parent_stdio()` flushes stdio
buffers and `dup2`s the saved fds back. The save/restore happens
unconditionally around `builtin_execute()` and around the structured
`pipeline_stage_drain()` call, so a builtin failure still leaves the
parent's stdio fds intact.

This fix is what made `test_m2_redirections.sh` go green (and is also
the reason `printenv > file` now works as expected).

## Adding a new integration test

1. Create `tests/integration/test_<milestone>_<feature>.sh`.
2. Copy the assertion skeleton from any existing script.
3. Use `./splash` as `SHELL_BIN`. Capture output with `OUT=$(echo '...' | $SHELL_BIN 2>/dev/null)` (or `2>&1` for stderr-checking tests).
4. Avoid relying on the user's environment — use `mktemp -d` for any
   filesystem ops, set `HOME` explicitly when testing tilde expansion,
   etc.
5. Make the script exit with `exit $FAIL` so the wrapper detects failures.
6. `chmod +x` the file (the runner uses `bash $t` so this is not strictly
   required, but it matches the rest of the suite).
7. Run `make integration-test` and confirm both your new script and the
   pre-existing ones still pass.
