# Executor & Pipes

## Design

The executor takes a `Pipeline` AST and runs it via `fork()`/`execvp()`. Two code paths: single command (simple fork+exec+wait) and multi-stage pipeline (N-1 pipes connecting N children).

## Implementation

### Single Command

1. `fork()` — check for error
2. Child: `execvp(argv[0], argv)` — on failure print error and `_exit(127)`
3. Parent: `waitpid()` — decode exit status via `WIFEXITED`/`WIFSIGNALED`

### Multi-Stage Pipeline

1. Allocate `N-1` pipes via `pipe()`
2. Fork each command:
   - Command `i > 0`: `dup2(pipes[i-1][0], STDIN_FILENO)` — read from previous pipe
   - Command `i < N-1`: `dup2(pipes[i][1], STDOUT_FILENO)` — write to next pipe
   - Close all pipe fds in child after dup2
   - `execvp()` — `_exit(127)` on failure
3. Parent: close all pipe fds immediately after forking all children
4. `waitpid()` for every child — return last command's exit status

### File Redirections (2.1–2.6)

Added `Redirection` struct to `SimpleCommand` — a dynamic array of `{type, target}` pairs. Six redirect types map directly to `open()` flag combinations:

| Syntax | RedirectType | open() flags | dup2 target |
|--------|-------------|-------------|-------------|
| `>` | `REDIRECT_OUTPUT` | `O_WRONLY\|O_CREAT\|O_TRUNC` | stdout |
| `>>` | `REDIRECT_APPEND` | `O_WRONLY\|O_CREAT\|O_APPEND` | stdout |
| `<` | `REDIRECT_INPUT` | `O_RDONLY` | stdin |
| `2>` | `REDIRECT_ERR` | `O_WRONLY\|O_CREAT\|O_TRUNC` | stderr |
| `>&` | `REDIRECT_OUT_ERR` | `O_WRONLY\|O_CREAT\|O_TRUNC` | stdout + stderr |
| `>>&` | `REDIRECT_APPEND_ERR` | `O_WRONLY\|O_CREAT\|O_APPEND` | stdout + stderr |

**Parser**: `parse_simple_command()` grammar is now `(WORD | redirection)+` where `redirection = REDIRECT_OP WORD`. Redirections can appear anywhere in the command (`> out ls -la` is valid). A command with only redirections and no words is a syntax error.

**Executor**: `apply_redirections()` runs in child process before `execvp()`. For pipelines, redirections apply after pipe wiring — so `ls 2> err.txt | grep foo > out.txt` correctly redirects stderr of `ls` and stdout of `grep` while pipes still connect stdout of `ls` to stdin of `grep`.

File permissions: new files created with mode `0644`.

### isatty() Check (2.9)

`main.c` checks `isatty(STDIN_FILENO)` at startup. Non-interactive mode (piped input, script input) suppresses the prompt and goodbye message. Required for integration testing and script execution.

### Background Execution

For `pipeline->background`: parent prints `[PID]` instead of calling `waitpid()`.

### File Descriptor Discipline

- Every `dup2()` is followed by closing all original pipe fds
- Parent closes all pipe fds before waiting
- On fork failure mid-pipeline: close all pipes, wait for already-forked children

### Error Handling

- `fork()` failure: print error, clean up, return -1
- `pipe()` failure: close already-created pipes, return -1
- `execvp()` failure: child prints `splash: <cmd>: <strerror>`, `_exit(127)`
- `waitpid()` failure: print error, continue waiting for remaining children

## REPL Loop

`main.c` implements the read-eval-print loop:
1. Print `splash> ` prompt, flush stdout
2. `fgets()` a line — EOF prints "Good bye!!" and exits
3. `tokenizer_tokenize()` → `parser_parse()` → `executor_execute()`
4. Free pipeline and token list on every iteration

## Testing

Milestone 1 verification criteria all pass:
- `ls`, `ls -al`, `/bin/echo hello world` — simple commands
- `ls | grep src` — two-stage pipeline
- `ls | grep c | wc -l` — three-stage pipeline
- Unknown command — proper error message
- Ctrl-D — clean exit
