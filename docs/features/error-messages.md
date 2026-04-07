# Error Messages Audit (10.3)

Audit and remediation of error-handling paths across the splash shell so that
every failure prints a helpful, contextual message in the standard format.

## Format

All error messages follow this convention:

```
splash: <context>: <strerror>
```

Examples:

- `splash: heredoc write: Broken pipe`
- `splash: tcsetpgrp (foreground): Operation not permitted`
- `splash: history: open '/Users/foo/.config/splash/history': Permission denied`
- `splash: command substitution: dup2 stdout: Bad file descriptor`

All error output goes to `stderr`, never `stdout`.

## What was audited

Every `.c` file in `src/` was reviewed for:

1. Unchecked syscalls (`tcsetpgrp`, `setpgid`, `tcsetattr`, `tcgetattr`,
   `dup2`, `open`, `write`, `read`).
2. Unchecked allocator calls (`malloc`, `strdup`, `realloc`).
3. Bare/uninformative error messages.
4. Error output going to `stdout` instead of `stderr`.
5. Missing context in error format.

## Issues fixed

### Critical (could crash or corrupt state)

| File | Issue | Fix |
|---|---|---|
| `editor.c:744` | `strdup(buf)` for `saved_line` not NULL-checked — would crash on OOM when navigating history | Fall back to `strdup("")` on failure |
| `editor.c:759,783` | `realloc(buf, cap)` failure silently kept the new larger `cap` value while `buf` still pointed to the old smaller allocation — buffer overflow on next write | Use temporary `newcap`; only update `cap` if realloc succeeds; otherwise truncate `elen` to fit existing capacity |
| `executor.c:48` | Heredoc body `write()` return value silently cast to `(void)` — silent data loss | Print `splash: heredoc write: <strerror>` on failure |

### High (silent failures)

| File | Issue | Fix |
|---|---|---|
| `config.c:97,106-107` | Three `strdup()` calls in `config_set()` not NULL-checked — could store NULL pointers in the config table | Allocate both into temporaries first; print OOM and free partial allocation on failure |
| `executor.c:493,548` | `setpgid()` calls in child and parent unchecked | Print `splash: setpgid (child/parent): <strerror>` on failure (skip benign `EACCES`/`EPERM`/`ESRCH`) |
| `executor.c:573,594,611` | Three `tcsetpgrp()` calls (foreground transfer, reclaim after stop, reclaim after exit) unchecked | Print contextual `splash: tcsetpgrp (foreground/reclaim/reclaim after stop): <strerror>` (skip `ENOTTY`) |
| `builtins.c:97,123,139` | `tcsetpgrp()` calls in `fg` builtin unchecked | Print `splash: fg: tcsetpgrp [(reclaim)]: <strerror>` (skip `ENOTTY`) |
| `main.c:153` | `tcsetpgrp()` at shell init unchecked | Print `splash: tcsetpgrp (init): <strerror>` (skip `ENOTTY`) |
| `editor.c:77` | `tcsetattr()` in `leave_raw_mode()` unchecked | Print `splash: tcsetattr (restore): <strerror>` |
| `editor.c:347` | `tcgetattr()` in `do_reverse_search()` escape handler unchecked — would have used uninitialized termios | Bail out cleanly with `SEARCH_CANCEL` if it fails |
| `expand.c:332,339` | Unchecked `dup2()` failures in command substitution child added bare `_exit(1)` with no message | Print `splash: command substitution: dup2 /dev/null/stdout: <strerror>` before exit |
| `history.c:94,98` | History file `fopen()` and `fprintf()` failed silently on disk full or permissions | Print `splash: history: open '<path>': <strerror>` and `splash: history: write: <strerror>` |

## Errors deliberately not changed

Some patterns were left as-is because adding error handling would not improve
robustness:

- **`fputs`/`fprintf`/`fputc` for table and pipeline display output** in
  `table.c`, `pipeline.c`. Display output is best-effort; if the terminal pipe
  breaks, the next syscall will surface the error.
- **`write()` in `term_write()`** for the line editor. Same reasoning.
- **`setpgid` `EACCES`/`EPERM`/`ESRCH`**: these are benign races when a child
  exits before the parent's `setpgid` runs, or when the shell is already in the
  intended group.
- **`tcsetpgrp` `ENOTTY`**: legitimate when running with stdin redirected from a
  pipe in interactive-detection edge cases.

## Testing

- Full unit test suite (1070 tests) passes under ASan + UBSan with zero
  warnings.
- Integration test pass/fail set is identical to baseline — no regressions
  introduced.
- Manual smoke tests confirm:
  - Normal stdout/stderr redirects work.
  - Heredocs still feed bodies correctly.
  - History file is written without errors.
  - `fg`/`bg`/job control flows still work in interactive mode.

## Related

- 10.1 Memory leak audit (sanitizer-clean codebase)
- 10.2 File descriptor leak audit (closed all leaked fds)
- 10.3 (this) Error messages audit (every error path now informative)
