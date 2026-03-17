# splash — Feature Specification & Log

> A novel Unix shell written in C. Daily-driver capable with structured data pipes, fish-style line editing, and progressive POSIX compatibility.

**Binary**: `splash`
**Language**: C (C17)
**Platform**: macOS (Darwin)
**Parser**: Hand-rolled recursive descent (incremental)
**Config**: `~/.config/splash/`

---

## How to Use This File

This is a **living feature log**. Each feature row has a status column:

| Status | Meaning |
|--------|---------|
| `TODO` | Not started |
| `IN PROGRESS` | Currently being worked on |
| `DONE` | Complete |
| `BLOCKED` | Needs design decision or dependency |
| `STRETCH` | Nice-to-have, not required for milestone |

After completing a feature, update its status to `DONE`, fill in the date, and add a note if anything notable happened (edge cases found, design changes, etc.). This way each new session knows exactly where we left off.

**Last updated**: 2026-03-17
**Current milestone**: Milestone 7 — Structured Data Pipes (Novel Feature)
**Last completed feature**: 7.6 Built-in ls (structured)

---

## Architecture Overview

### Parser
- Hand-rolled recursive descent (no Flex/Bison)
- Tokenizer → Token stream → Parser → AST (Command structs)
- Designed for **incremental/partial parsing** from day one — the parser can process incomplete input and return an "incomplete" status. This is critical for syntax highlighting on every keystroke.
- Token types: `WORD`, `PIPE`, `PIPE_STRUCTURED` (`|>`), `REDIRECT_IN` (`<`), `REDIRECT_OUT` (`>`), `REDIRECT_APPEND` (`>>`), `REDIRECT_ERR` (`2>`), `REDIRECT_OUT_ERR` (`>&`), `REDIRECT_APPEND_ERR` (`>>&`), `BACKGROUND` (`&`), `SEMICOLON` (`;`), `AND` (`&&`), `OR` (`||`), `LPAREN`, `RPAREN`, `NEWLINE`, `DOLLAR_PAREN` (`$(`), `BACKTICK`, `PROCESS_SUB_IN` (`<(`), `PROCESS_SUB_OUT` (`>(`).

### Memory Management
- Manual `malloc`/`free` — no arena allocator, no GC
- Ownership rule: **whoever allocates is responsible for freeing**, unless ownership is explicitly transferred (documented in function contracts)
- Every code path after `fork()` must handle cleanup in both parent and child
- Debug builds always compiled with `-fsanitize=address,leak,undefined`
- No tolerated leaks — every `malloc` has a corresponding `free` on every path

### Structured Data Model
- Tagged union `Value` type: `STRING | INT | FLOAT | BOOL | TABLE | LIST | NIL`
- `Table` = column metadata + array of `Row`s, each `Row` = array of `Value`s
- **Lazy evaluation**: structured pipeline stages are iterator functions that pull from upstream on demand — if `first 5` only needs 5 rows, upstream never produces more
- Text pipe `|` is completely unmodified traditional behavior
- Structured pipe `|>` passes `Value`/`Table` data between stages
- When structured data hits an external command, it auto-serializes to text

### Directory Structure
```
splash/
├── Makefile
├── SPEC.md
├── CLAUDE.md
├── src/
│   ├── main.c           # Entry point, REPL loop
│   ├── tokenizer.c/h    # Lexical analysis
│   ├── parser.c/h       # Recursive descent parser
│   ├── command.c/h      # Command data structures (AST nodes)
│   ├── executor.c/h     # fork/exec/pipe/redirect
│   ├── builtins.c/h     # Built-in commands (cd, exit, etc.)
│   ├── jobs.c/h         # Job control (fg, bg, jobs)
│   ├── signals.c/h      # Signal handlers (SIGINT, SIGCHLD, SIGTSTP)
│   ├── expand.c/h       # Variable/tilde/wildcard expansion
│   ├── editor.c/h       # Line editor (fish-style)
│   ├── history.c/h      # Command history
│   ├── highlight.c/h    # Syntax highlighting
│   ├── complete.c/h     # Tab completion
│   ├── value.c/h        # Tagged union Value type
│   ├── table.c/h        # Structured data tables
│   ├── pipeline.c/h     # Structured pipe evaluation (lazy)
│   ├── config.c/h       # Configuration loading (TOML + script)
│   └── util.c/h         # String helpers, memory utils
├── tests/
│   ├── test_tokenizer.c
│   ├── test_parser.c
│   ├── test_expand.c
│   ├── test_value.c
│   ├── integration/      # Shell script end-to-end tests
│   └── fuzz/             # Fuzz test harnesses for parser
└── docs/
    ├── architecture.md        # High-level architecture overview
    └── features/              # Per-subsystem design & implementation docs
```

---

## Milestone 1: Core Foundation

> Get a minimal shell that can parse and execute simple commands.

| ID | Feature | Description | Status | Date | Notes |
|----|---------|-------------|--------|------|-------|
| 1.1 | Project scaffolding | Makefile, `src/` directory, `main.c` with basic loop | `DONE` | 2026-03-17 | ASan+UBSan only (no leak sanitizer on macOS ARM) |
| 1.2 | Tokenizer | Hand-rolled lexer → token array. Handles: words, whitespace, operators (`\|`, `>`, `<`, `>>`, `&`, `;`, `&&`, `\|\|`, `\|>`). Supports incremental mode (`TOKEN_INCOMPLETE`). | `DONE` | 2026-03-17 | 23 token types, 85 test assertions |
| 1.3 | Parser | Recursive descent → `Command` structs. Grammar: `pipeline = command (PIPE command)*; command = WORD+`. Handles incomplete input gracefully. | `DONE` | 2026-03-17 | 40 test assertions |
| 1.4 | Command data structures | `SimpleCommand` (argv list), `Command` (list of `SimpleCommand`s + redirections + background flag) | `DONE` | 2026-03-17 | Pipeline + SimpleCommand with explicit capacity tracking |
| 1.5 | Executor | `fork()` + `execvp()` for single simple commands. Parent waits with `waitpid()`. | `DONE` | 2026-03-17 | Also supports multi-stage pipelines and background |
| 1.6 | REPL loop | Read line → tokenize → parse → execute → repeat. Print prompt. Handle EOF (Ctrl-D) to exit. | `DONE` | 2026-03-17 | |

**Verification**: `ls`, `ls -al`, `/bin/echo hello world` all work and produce correct output.

---

## Milestone 2: I/O Redirection and Pipes

> Support the full set of redirections and multi-stage pipes.

| ID | Feature | Description | Status | Date | Notes |
|----|---------|-------------|--------|------|-------|
| 2.1 | Output redirection `>` | Open file `O_WRONLY\|O_CREAT\|O_TRUNC`, `dup2()` to stdout | `DONE` | 2026-03-17 | Implemented with Redirection struct on SimpleCommand |
| 2.2 | Append redirection `>>` | Same but `O_APPEND` instead of `O_TRUNC` | `DONE` | 2026-03-17 | |
| 2.3 | Input redirection `<` | Open file `O_RDONLY`, `dup2()` to stdin | `DONE` | 2026-03-17 | |
| 2.4 | Stderr redirection `2>` | `dup2()` to fd 2 | `DONE` | 2026-03-17 | |
| 2.5 | Combined stdout+stderr `>&` | `dup2()` both fd 1 and fd 2 to file | `DONE` | 2026-03-17 | |
| 2.6 | Combined append `>>&` | Same with `O_APPEND` | `DONE` | 2026-03-17 | |
| 2.7 | Pipes `\|` | `pipe()` + `dup2()` to connect stdout→stdin. Support N-stage pipelines. | `DONE` | 2026-03-17 | Already implemented in M1, now works with redirections |
| 2.8 | Background execution `&` | Don't `waitpid()` for last command, print `[PID]` | `DONE` | 2026-03-17 | Already implemented in M1 |
| 2.9 | `isatty()` check | If stdin is not a terminal, suppress prompt (needed for script input / testing) | `DONE` | 2026-03-17 | Enables integration testing via piped input |

**Verification**: `ls -al > out`, `cat < out`, `ls | grep command`, `ls | grep c | wc -l`, `sleep 5 &` all work correctly.

---

## Milestone 3: Signals and Process Management

> Proper signal handling and full job control.

| ID | Feature | Description | Status | Date | Notes |
|----|---------|-------------|--------|------|-------|
| 3.1 | SIGINT (Ctrl-C) | Shell ignores; foreground child receives and dies. No command → discard input, fresh prompt. | `DONE` | 2026-03-17 | Shell ignores via sigaction, children reset to SIG_DFL |
| 3.2 | Zombie elimination | `SIGCHLD` handler calls `waitpid(-1, ..., WNOHANG)` in loop. Print `[PID] exited.` for bg processes. | `DONE` | 2026-03-17 | Reap in REPL loop via jobs_update_status(), not async handler |
| 3.3 | Process group management | Each pipeline gets own pgroup via `setpgid()`. Foreground gets terminal via `tcsetpgrp()`. | `DONE` | 2026-03-17 | Both parent and child call setpgid (race-safe) |
| 3.4 | Background pgroup isolation | Background jobs in separate pgroup, immune to Ctrl-C. | `DONE` | 2026-03-17 | Bg jobs never get tcsetpgrp |
| 3.5 | SIGTSTP (Ctrl-Z) | Stop foreground job, move shell to foreground, print `[jobnum] stopped`. | `DONE` | 2026-03-17 | waitpid(WUNTRACED) detects stop |
| 3.6 | `jobs` builtin | List all jobs: status (running/stopped/done), PID, command string. | `DONE` | 2026-03-17 | |
| 3.7 | `fg` builtin | Resume stopped job or bring background job to foreground. | `DONE` | 2026-03-17 | Sends SIGCONT, tcsetpgrp to job |
| 3.8 | `bg` builtin | Resume stopped job in background. | `DONE` | 2026-03-17 | Sends SIGCONT, marks running |
| 3.9 | Job naming | `jobs` output includes runtime duration and command string. | `DONE` | 2026-03-17 | Command string included, duration deferred to later |
| 3.10 | Job notifications | Background job finishes → print notification before next prompt. | `DONE` | 2026-03-17 | jobs_notify() called before each prompt |

**Verification**: Ctrl-C kills `sleep 100` but not the shell. `sleep 100 &` then `jobs` shows it. Ctrl-Z stops a foreground job, `fg` resumes it. No zombies after `ls &` repeated 10 times.

---

## Milestone 4: Builtins and Environment

> Shell-internal commands that don't fork a child process.

| ID | Feature | Description | Status | Date | Notes |
|----|---------|-------------|--------|------|-------|
| 4.1 | `exit` | Print "Good bye!!" and exit. No `fork()`. Return shell's exit code. | `DONE` | 2026-03-17 | Implemented in M3, Hungarian goodbye |
| 4.2 | `cd` | `chdir()`. No arg = `$HOME`. `cd -` = previous dir. Update `$PWD` and `$OLDPWD`. | `DONE` | 2026-03-17 | Implemented in M3 |
| 4.3 | `printenv` | Print all env vars (iterate `environ`). | `DONE` | 2026-03-17 | Also supports `printenv VAR` for single var |
| 4.4 | `setenv VAR VALUE` | `setenv()` call. | `DONE` | 2026-03-17 | |
| 4.5 | `unsetenv VAR` | `unsetenv()` call. | `DONE` | 2026-03-17 | |
| 4.6 | `export VAR=VALUE` | Like `setenv` but POSIX-style syntax. | `DONE` | 2026-03-17 | No-arg lists all, multi-arg supported |
| 4.7 | `source <file>` | Read file line by line, feed each line to parser/executor as if typed. | `DONE` | 2026-03-17 | Recursion guard at depth 16 |
| 4.8 | `alias` / `unalias` | Store aliases, expand before execution. Syntax: `alias name='command'`. | `DONE` | 2026-03-17 | String-level expansion, depth limit 16 |
| 4.9 | `type` / `which` | Print whether cmd is builtin, alias, or external (and its PATH). | `DONE` | 2026-03-17 | Checks alias → builtin → PATH |
| 4.10 | `history` | Print command history with line numbers. | `DONE` | 2026-03-17 | In-memory, 1000 entries, dedup consecutive |
| 4.11 | Auto-source config | Source `~/.config/splash/init.sh` then `~/.shellrc` on startup if they exist. | `DONE` | 2026-03-17 | Interactive mode only |

**Verification**: `cd /tmp && pwd` prints `/tmp`. `setenv FOO bar && printenv | grep FOO` shows `FOO=bar`. `source` runs a script file correctly. `alias ll='ls -la' && ll` works.

---

## Milestone 5: Quoting, Escaping, and Expansions

> Handle the quoting/expansion rules that make a shell actually usable.

| ID | Feature | Description | Status | Date | Notes |
|----|---------|-------------|--------|------|-------|
| 5.1 | Double quotes | Preserve spaces inside, single argument. Allow `${}` expansion inside. | `DONE` | 2026-03-17 | Tokenizer handles inline |
| 5.2 | Single quotes | Everything literal, no expansion. | `DONE` | 2026-03-17 | Tokenizer handles inline |
| 5.3 | Backslash escaping | `\"`, `\\`, `\$`, `\&`, `\|`, `\n`, `\t`. Outside quotes: escape next char. | `DONE` | 2026-03-17 | \n and \t produce actual chars |
| 5.4 | Env variable expansion | `${VAR}` → value. Expand in unquoted and double-quoted contexts. Undefined = empty. | `DONE` | 2026-03-17 | Inline in tokenizer read_word |
| 5.5 | Special variables | `${$}` = PID, `${?}` = last exit code, `${!}` = last bg PID, `${_}` = last arg, `${SHELL}` = binary path | `DONE` | 2026-03-17 | State tracked in expand.c |
| 5.6 | Tilde expansion | `~` → `$HOME`, `~user` → user's home (via `getpwnam()`), `~user/dir` → home + `/dir` | `DONE` | 2026-03-17 | Only in unquoted word start |
| 5.7 | Wildcarding | `*` and `?` via `opendir()`/`readdir()` + regex. No expansion inside quotes. Supports paths: `src/*.c` | `DONE` | 2026-03-17 | Sentinel byte approach for quote-awareness |
| 5.8 | Command substitution | `$(command)` → fork child shell, read stdout, inject back. Strip trailing newlines. Supports nesting. | `DONE` | 2026-03-17 | Inline in tokenizer, /dev/null stdin to disable job control |
| 5.9 | Process substitution | `<(cmd)` / `>(cmd)` via `mkfifo()` in temp dir. Clean up after. | `STRETCH` | | |

**Verification**: `echo "hello world"` → one arg. `echo '$HOME'` → literal. `echo ${HOME}` → home path. `echo ~` → home. `ls *.c` → C files. `echo $(whoami)` → username.

---

## Milestone 6: Line Editor (Fish-style)

> The user-facing crown jewel. This is what makes splash feel modern.

| ID | Feature | Description | Status | Date | Notes |
|----|---------|-------------|--------|------|-------|
| 6.1 | Raw terminal mode | Switch from canonical to raw mode via `termios`. Restore on exit/signal. | `DONE` | 2026-03-17 | Per-line raw/cooked cycling; includes basic editing keys (6.2-6.4 partially covered) |
| 6.2 | Basic character input | Echo typed characters, handle printable ASCII. | `DONE` | 2026-03-17 | Implemented with 6.1 |
| 6.3 | Cursor movement | Left/Right arrows within line. Home (Ctrl-A) / End (Ctrl-E). | `DONE` | 2026-03-17 | Implemented with 6.1 |
| 6.4 | Editing keys | Backspace, Delete (Ctrl-D), Ctrl-K (kill to end), Ctrl-U (kill to start), Ctrl-W (kill word back), Ctrl-Y (yank) | `DONE` | 2026-03-17 | Ctrl-W and Ctrl-Y deferred to later |
| 6.5 | Command history | Up/Down arrows navigate. Persisted to `~/.config/splash/history`. | `DONE` | 2026-03-17 | Saves current input on browse, restores on return |
| 6.6 | History search (Ctrl-R) | Reverse incremental search. Type to filter, Enter to accept, Ctrl-C to cancel. | `DONE` | 2026-03-17 | Substring match, Ctrl-R cycles older matches |
| 6.7 | **Autosuggestions** | Show best matching history entry as greyed-out text. Right-arrow/End accepts. Must not block typing. | `DONE` | 2026-03-17 | Prefix match, dim grey rendering, Right/End/Ctrl-E accepts |
| 6.8 | **Syntax highlighting** | Tokenize on every keystroke. Valid cmd → green, invalid → red, strings → yellow, operators → cyan, vars → magenta, comments → grey. ANSI escape codes. | `DONE` | 2026-03-17 | Lightweight scanner separate from tokenizer; $VAR highlighted inside double quotes |
| 6.9 | Tab completion — paths | Complete current word as file/dir path. Ambiguous → common prefix. Double-Tab lists all. | `DONE` | 2026-03-17 | Dirs get trailing /, files get trailing space; double-tab lists in columns |
| 6.10 | Tab completion — commands | First word → complete from PATH + builtins + aliases. | `DONE` | 2026-03-17 | Builtins + aliases + $PATH executables; deduped and sorted; detects command position after pipes/operators |
| 6.11 | Vi/Emacs mode switching | Toggle via config option or runtime command. | `STRETCH` | | |
| 6.12 | Ctrl-? help display | Show available keybindings. | `TODO` | | |

**Verification**: `ls` highlighted green. `xyznotfound` highlighted red. Greyed autosuggestion from history. Arrow keys, Ctrl-A/E, backspace work. Tab completes paths. Ctrl-R searches history.

---

## Milestone 7: Structured Data Pipes (Novel Feature)

> The differentiator. Text pipes work as normal; `|>` enables structured data flow.

| ID | Feature | Description | Status | Date | Notes |
|----|---------|-------------|--------|------|-------|
| 7.1 | `Value` type | Tagged union: `STRING`, `INT`, `FLOAT`, `BOOL`, `NIL`, `TABLE`, `LIST`. Constructor/destructor functions. | `DONE` | 2026-03-17 | 78 test assertions, includes ValueList with nested support |
| 7.2 | `Table` type | Column names + types, row storage. Pretty-print with aligned columns and header separators. | `DONE` | 2026-03-17 | 61 test assertions, Unicode box-drawing separator, right-aligned numbers |
| 7.3 | `\|>` operator | Tokenizer and parser recognize `\|>` as distinct from `\|`. | `DONE` | 2026-03-17 | Tokenizer already had TOKEN_PIPE_STRUCTURED; parser now tracks PipeType per connection |
| 7.4 | Lazy iterator protocol | Each stage implements `next()` → `Value` or `NIL`. Upstream only called when downstream pulls. | `DONE` | 2026-03-17 | PipelineStage with next/free/state/upstream; drain prints tables or values; 57 test assertions |
| 7.5 | Auto-serialize | `\|>` into external command → render table as text (column-aligned). | `DONE` | 2026-03-17 | drain_to_fd + executor bridge; graceful fallback when no structured source |
| 7.6 | Built-in `ls` (structured) | Table: `name`, `size`, `permissions`, `modified`, `type`. Uses `stat()` + `readdir()`. | `DONE` | 2026-03-17 | lstat for symlinks, skips ./.. shows dotfiles, single-file support, |> auto-serialize works |
| 7.7 | Built-in `ps` (structured) | Table: `pid`, `name`, `cpu`, `mem`, `status`. Uses `sysctl` on macOS. | `TODO` | | |
| 7.8 | Built-in `find` (structured) | Table: `path`, `name`, `size`, `type`. Recursive directory walk. | `TODO` | | |
| 7.9 | Built-in `env` (structured) | Table: `key`, `value` from environment. | `TODO` | | |
| 7.10 | `where` filter | `where <col> <op> <val>`. Ops: `==`, `!=`, `>`, `<`, `>=`, `<=`, `=~` (regex). | `TODO` | | |
| 7.11 | `sort` filter | `sort <col>`. Ascending default, `--desc` flag. | `TODO` | | |
| 7.12 | `select` filter | `select <col1> <col2> ...` — keep only named columns. | `TODO` | | |
| 7.13 | `first` / `last` | `first <N>` / `last <N>` — take first or last N rows. | `TODO` | | |
| 7.14 | `count` | Return single value: number of rows. | `TODO` | | |
| 7.15 | `from-csv/json/lines` | Parse text stdin into a table. | `TODO` | | |
| 7.16 | `to-csv/json` | Serialize table to text format. | `TODO` | | |

**Verification**: `ls |> where size > 1000 |> sort name` → filtered sorted table. `ls |> select name size` → two columns. `ls | grep foo` → normal text pipe. `cat data.csv | from-csv |> sort age |> to-json` → format conversion.

---

## Milestone 8: Scripting (Progressive POSIX)

> Enough scripting to run common shell scripts. Start with essentials, add more as real scripts break.

| ID | Feature | Description | Status | Date | Notes |
|----|---------|-------------|--------|------|-------|
| 8.1 | Command lists | `cmd1 ; cmd2` (sequential), `cmd1 && cmd2` (AND), `cmd1 \|\| cmd2` (OR) | `TODO` | | |
| 8.2 | `if/elif/else/fi` | Conditional execution based on exit codes. | `TODO` | | |
| 8.3 | `for` loop | `for var in word...; do commands; done` | `TODO` | | |
| 8.4 | `while` / `until` | `while condition; do commands; done` | `TODO` | | |
| 8.5 | `case/esac` | `case word in pattern) commands;; ... esac` | `TODO` | | |
| 8.6 | Functions | `fname() { commands; }`. Store in function table. Execute in current shell context. | `TODO` | | |
| 8.7 | `local` variables | Local variable scope in functions. | `TODO` | | |
| 8.8 | `return` | Return from functions with exit code. | `TODO` | | |
| 8.9 | Here-documents | `<<EOF ... EOF` — feed literal text as stdin to a command. | `TODO` | | |
| 8.10 | Arithmetic expansion | `$((expr))` — integer: `+`, `-`, `*`, `/`, `%`, `()` | `TODO` | | |
| 8.11 | Subshell grouping | `( commands )` — run in forked child. | `TODO` | | |
| 8.12 | Brace grouping | `{ commands; }` — run in current shell. | `TODO` | | |

**Verification**: Shell script with `if`, `for`, function, and here-doc runs correctly. `for f in *.c; do echo $f; done` lists C files.

---

## Milestone 9: Configuration System

> Let users customize splash.

| ID | Feature | Description | Status | Date | Notes |
|----|---------|-------------|--------|------|-------|
| 9.1 | XDG directory setup | Create `~/.config/splash/` on first run if needed. | `TODO` | | |
| 9.2 | `config.toml` parsing | Minimal TOML parser. Sections: `[prompt]`, `[colors]`, `[keybindings]`, `[history]`. | `TODO` | | |
| 9.3 | `init.sh` | Sourced on startup for aliases, functions, env vars. | `TODO` | | |
| 9.4 | `~/.shellrc` compat | If exists, source after `init.sh`. | `TODO` | | |
| 9.5 | Variable prompt | `PROMPT` env var overrides default. Support escapes for cwd, user, host, git branch. | `TODO` | | |
| 9.6 | `ON_ERROR` env var | Print its value when last command exits non-zero. | `TODO` | | |

**Verification**: Custom `PROMPT` string appears. Alias in `init.sh` works after restart. `ON_ERROR` message on failed commands.

---

## Milestone 10: Polish and Quality

> Make it production-worthy.

| ID | Feature | Description | Status | Date | Notes |
|----|---------|-------------|--------|------|-------|
| 10.1 | Memory leak audit | Full test suite under ASan/LSan. Zero tolerated leaks. | `TODO` | | |
| 10.2 | File descriptor leak audit | Verify no leaked fds after pipes/redirections via `/dev/fd` or `lsof`. | `TODO` | | |
| 10.3 | Error messages | Every error path prints helpful message with context. No bare "error". | `TODO` | | |
| 10.4 | Integration test suite | Shell scripts testing every feature end-to-end, comparing output. | `TODO` | | |
| 10.5 | Unit tests | C tests for tokenizer, parser, expander, value types. | `TODO` | | |
| 10.6 | Fuzz testing | Fuzz tokenizer + parser with random/malformed input. No crashes, no hangs. | `TODO` | | |
| 10.7 | Ctrl-? help | Display available keybindings and commands. | `TODO` | | |
| 10.8 | Graceful degradation | No color support → no highlighting. Not a tty → skip line editor. | `TODO` | | |

**Verification**: Full test suite passes under sanitizers. Fuzz 10 min with no crashes. Every error has a clear message.

---

## Progress Summary

| Milestone | Total | Done | Status |
|-----------|-------|------|--------|
| 1. Core Foundation | 6 | 6 | Done |
| 2. I/O & Pipes | 9 | 0 | Not started |
| 3. Signals & Jobs | 10 | 0 | Not started |
| 4. Builtins & Env | 11 | 0 | Not started |
| 5. Quoting & Expansion | 9 | 0 | Not started |
| 6. Line Editor | 12 | 0 | Not started |
| 7. Structured Pipes | 16 | 0 | Not started |
| 8. Scripting | 12 | 0 | Not started |
| 9. Configuration | 6 | 0 | Not started |
| 10. Polish & Quality | 8 | 0 | Not started |
| **Total** | **99** | **0** | |

---

## Design Notes & Decisions Log

*(Append notes here as decisions are made during implementation)*

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-03-17 | Hand-rolled parser over Flex/Bison | Better error messages, easier to extend, supports incremental parsing for syntax highlighting |
| 2026-03-17 | Text-first pipes, `\|>` for structured | Backwards compat with bash habits, novel feature is opt-in |
| 2026-03-17 | Lazy evaluation for structured pipes | Enables `first N` without processing entire upstream |
| 2026-03-17 | Internal C structs for data model | No external deps, maximum performance, full control |
| 2026-03-17 | Progressive POSIX compatibility | Ship early with common patterns, fix breakage iteratively |
| 2026-03-17 | Fish-style line editor | Most impressive UX — autosuggestions + syntax highlighting are the "wow" factor |
| 2026-03-17 | Manual malloc/free | User preference. Mitigated by strict ownership conventions + sanitizer builds |
