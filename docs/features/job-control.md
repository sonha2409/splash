# Job Control & Signals

## Design

Job control requires three interacting subsystems: signal handling, process group management, and a job table. All three must work together for correct terminal behavior.

### Why process groups?

Without process groups, Ctrl-C sends SIGINT to every process sharing the terminal — including the shell. By placing each pipeline in its own process group and using `tcsetpgrp()` to designate which group owns the terminal, only the foreground job receives terminal signals.

## Implementation

### Signal Setup (`signals.c`)

At startup, the shell ignores:
- `SIGINT` — Ctrl-C should kill children, not the shell
- `SIGTSTP` — Ctrl-Z should stop children, not the shell
- `SIGTTOU` / `SIGTTIN` — prevent the shell from stopping when it calls `tcsetpgrp()`

All signals use `sigaction()` (never `signal()`). Children reset to `SIG_DFL` before `exec` via `signals_default()`.

### Process Group Management (`executor.c`)

For every pipeline:
1. First child: `setpgid(0, 0)` — creates a new process group (pgid = own pid)
2. Other children: `setpgid(0, pgid)` — join the first child's group
3. Parent also calls `setpgid(child, pgid)` — race-safe (both parent and child do it)
4. Foreground: `tcsetpgrp(STDIN_FILENO, pgid)` gives the terminal to the job
5. After job exits/stops: `tcsetpgrp(STDIN_FILENO, shell_pgid)` reclaims terminal

Process groups and `tcsetpgrp()` are only used in interactive mode (`isatty(STDIN_FILENO)`). Non-interactive mode (piped input) skips these to avoid errors.

### Job Table (`jobs.c`)

Fixed-size table (`MAX_JOBS = 64`). Each slot stores:
- Job ID (sequential, assigned at creation)
- Process group ID (pgid)
- Array of all PIDs in the pipeline
- Status: `RUNNING`, `STOPPED`, `DONE`
- Original command string for display

Key operations:
- `jobs_add()` — find first empty slot, copy PID array and command string
- `jobs_update_status()` — iterate table, `waitpid(WNOHANG | WUNTRACED)` each PID
- `jobs_notify()` — print "done" for completed background jobs, remove them
- `jobs_print()` — list all jobs for the `jobs` builtin

### Builtins (`builtins.c`)

Builtins run in the parent process (no fork):
- `exit [N]` — print goodbye, `exit(N)`
- `cd [dir]` — `chdir()`, updates `$PWD` and `$OLDPWD`. `cd -` goes to `$OLDPWD`.
- `jobs` — calls `jobs_print()`
- `fg [%N]` — `tcsetpgrp()` to job's pgid, `SIGCONT` if stopped, `waitpid(WUNTRACED)`
- `bg [%N]` — `SIGCONT` to job's pgid, mark as running

### Ctrl-Z Flow

1. User presses Ctrl-Z → kernel sends `SIGTSTP` to foreground process group
2. Shell has `SIGTSTP` ignored, so it's unaffected
3. Foreground child stops → `waitpid()` returns with `WIFSTOPPED`
4. Executor marks job as `JOB_STOPPED`, prints `[N] stopped  command`
5. Shell reclaims terminal with `tcsetpgrp()`

### Ctrl-C Flow

1. User presses Ctrl-C → kernel sends `SIGINT` to foreground process group
2. Shell has `SIGINT` ignored → survives
3. Child has `SIGINT` at default → dies
4. `waitpid()` returns with `WIFSIGNALED` → shell prints new prompt

## Edge Cases

- `setpgid()` race: both parent and child call it, so whichever runs first wins
- `waitpid()` loop handles `EINTR` (interrupted by signals)
- Non-interactive mode skips all terminal/pgroup operations
- Background jobs never get `tcsetpgrp()` → immune to Ctrl-C

## Testing

Integration tests cover:
- Shell survives commands, background jobs launch
- `jobs` shows running background processes
- `exit` and `cd` builtins work correctly
- Multi-stage pipes work with process groups
- Redirections remain functional
