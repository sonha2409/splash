# Builtins and Environment

## Overview

Shell builtins are commands that execute inside the shell process itself — no `fork()`/`exec()` needed. This is required for commands like `cd` (which must modify the shell's own working directory) and `exit` (which must terminate the shell process).

## Design

All builtins are dispatched through two functions in `builtins.c`:

- `builtin_is_builtin(name)` — returns 1 if the command name is a builtin
- `builtin_execute(cmd)` — executes the builtin, returns exit status

The executor checks `builtin_is_builtin()` before forking. For single-command (non-pipeline) foreground invocations, builtins run in-process. In pipelines, builtins would need to fork (not yet implemented — currently they only run as standalone commands).

### Why builtins must run in-process

- `cd` calls `chdir()`, which only affects the calling process
- `exit` calls `exit()` to terminate the shell
- `setenv`/`export`/`unsetenv` modify the environment of the shell process
- If these ran in a child process, the changes would be lost when the child exits

## Implemented Builtins

### Process control (Milestone 3)
- **`exit [status]`** — prints goodbye message, exits with given status (default 0)
- **`cd [dir]`** — `chdir()`. No arg = `$HOME`. `cd -` = `$OLDPWD`. Updates `$PWD` and `$OLDPWD`
- **`jobs`** — prints the job table via `jobs_print()`
- **`fg [%N]`** — brings a job to foreground, gives it the terminal, waits
- **`bg [%N]`** — continues a stopped job in the background

### Environment (Milestone 4)
- **`printenv [VAR]`** — no args: prints all env vars. With arg: prints that var's value (exit 1 if not found)
- **`setenv VAR VALUE`** — calls `setenv(name, value, 1)`. Requires exactly 2 args
- **`unsetenv VAR`** — calls `unsetenv(name)`. Requires exactly 1 arg
- **`export [VAR=VALUE]`** — no args: prints all vars with `export` prefix. With args: parses `=` to split name/value and calls `setenv()`. `export VAR` (no `=`) is a no-op

## Edge Cases

- `printenv` with nonexistent var returns exit status 1, no output
- `setenv`/`unsetenv` with wrong arg count prints usage to stderr
- `export VAR=` sets the variable to an empty string (valid)
- `export VAR` without `=` is silently accepted (mirrors bash behavior)
- `cd -` prints the directory it switches to (the old `$OLDPWD`)
- `cd` to nonexistent directory prints error with `strerror(errno)`

### File sourcing (Milestone 4)
- **`source <file>`** — reads file line by line through tokenizer → parser → executor. Supports nested source with a depth guard of 16 levels. Shares the `executor_execute_line()` helper with the REPL loop.

## Architecture Note

The `executor_execute_line()` function in `executor.c` provides a shared entry point for executing a single line of input (tokenize → parse → execute). Both `main.c` (REPL loop) and `builtins.c` (`source`) use this, avoiding code duplication.

## Testing

- **Integration tests** in `tests/integration/test_m4_builtins.sh` — 24 tests covering features 4.1–4.7
- Tests verify: correct output, error messages for bad usage, variable persistence across commands, unset behavior, source execution, nested source, source error handling
