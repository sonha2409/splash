# CLAUDE.md

Ground rules for Claude Code when working in this repository.

## Project

**splash** — A novel Unix shell written in C.

- **Repo**: `sonha2409/shell` (main branch)
- **Spec**: See `SPEC.md` for the full feature specification, progress tracker, and architecture decisions
- **Platform**: macOS (Darwin)
- **Language**: C (C17), no external dependencies

## User Interaction

- **Challenge unreliable or non-best-practice approaches**: If the user gives instructions, commands, or approaches that are unreliable, error-prone, or not industry best practice for systems programming, **warn them and explain the concern before proceeding**. This is especially important for C code — unsafe memory patterns, unchecked syscalls, signal-unsafe functions, etc. Suggest the recommended alternative. Only proceed with the original approach if the user explicitly confirms.
- **Stay within this repository**: Do NOT reference, read, or copy from any other project folders on this machine. All code should be written from scratch within `/Users/sonhathai/shell/`.
- **No Co-Authored-By**: Never include `Co-Authored-By` lines, Claude credit, or any co-author notes in commit messages.

## Architecture

```
splash/
├── Makefile
├── SPEC.md
├── CLAUDE.md
├── src/
│   ├── main.c           # Entry point, REPL loop
│   ├── tokenizer.c/h    # Lexical analysis (hand-rolled)
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
│   ├── test_tokenizer.c  # Unit tests
│   ├── test_parser.c
│   ├── test_expand.c
│   ├── test_value.c
│   ├── integration/       # Shell script end-to-end tests
│   └── fuzz/              # Fuzz test harnesses
└── docs/
    ├── architecture.md    # High-level architecture overview
    └── features/          # Per-subsystem design & implementation docs
        ├── tokenizer.md
        ├── parser.md
        ├── pipes-and-redirection.md
        ├── job-control.md
        ├── line-editor.md
        ├── structured-pipes.md
        └── ...
```

## Coding Standards (C)

### Style

- **K&R brace style**: opening brace on same line as statement
- **4-space indentation**, no tabs
- **`snake_case`** for all identifiers: functions, variables, types, files
- **`UPPER_SNAKE_CASE`** for macros and constants
- **Include guards** in every header: `#ifndef SPLASH_TOKENIZER_H` / `#define SPLASH_TOKENIZER_H`
- Max line length: 100 characters (soft limit — don't break readability to hit it)
- One blank line between functions, two blank lines between sections

### Naming

- Prefix public functions with their module: `tokenizer_init()`, `parser_parse()`, `value_free()`
- Static (file-local) helper functions: no prefix needed
- Structs: `typedef struct { ... } TokenList;` — `PascalCase` for type names
- Enums: `typedef enum { TOKEN_WORD, TOKEN_PIPE, ... } TokenType;`

### Headers

- Every `.c` file has a matching `.h` file
- Headers contain only declarations — no implementations (except small `static inline` functions)
- Include what you use — no transitive dependency assumptions
- System headers first, then project headers, alphabetical within each group

### Functions

- Every function should do one thing
- Max function length: ~50 lines (guideline, not hard rule — some parsers will be longer)
- Document ownership in function comments:
  ```c
  // Creates a new command. Caller takes ownership and must call command_free().
  Command *command_new(void);
  ```

## Memory Management

This is the most critical discipline in this project. C has no safety net.

- **Ownership rule**: Whoever allocates is responsible for freeing, unless ownership is explicitly transferred. Document transfers in function comments.
- **Every `malloc`/`calloc`/`realloc`** must be checked for `NULL` return.
- **Every `fork()` path** must handle cleanup in both parent and child. The child should not free the parent's data — it will `exec` or `_exit`.
- **Every `open()`/`dup2()`/`pipe()`** must close file descriptors on every path (success and error).
- **No leaked file descriptors**: After `dup2()`, close the original fd. After `pipe()`, close unused ends in both parent and child.
- **Debug builds**: Always compiled with `-fsanitize=address,leak,undefined`. Zero tolerated leaks or UB.
- **Before marking any feature done**: Run the test suite under sanitizers and verify clean output.

## Error Handling

- **No silent failures**. Every syscall that can fail must be checked.
- **`perror()`-style messages**: Include context about what was being done.
  ```c
  // Good:
  if (dup2(fd, STDOUT_FILENO) == -1) {
      fprintf(stderr, "splash: redirect stdout to '%s': %s\n", filename, strerror(errno));
      _exit(1);
  }

  // Bad:
  dup2(fd, STDOUT_FILENO);  // unchecked
  ```
- **Error messages go to stderr**, never stdout.
- **Format**: `splash: <context>: <system error message>` — mirrors how bash reports errors.
- **Child processes**: Use `_exit()` not `exit()` after `fork()` to avoid flushing parent's stdio buffers.

## Build System

- `make` — build release binary (`splash`)
- `make debug` — build with `-g -O0 -fsanitize=address,leak,undefined -DDEBUG`
- `make test` — build and run all unit tests + integration tests
- `make fuzz` — build and run fuzz test harnesses
- `make clean` — remove all build artifacts
- Compiler: `cc` (Apple Clang on macOS)
- Flags (release): `-Wall -Wextra -Werror -pedantic -std=c17 -O2`
- Flags (debug): `-Wall -Wextra -Werror -pedantic -std=c17 -g -O0 -fsanitize=address,leak,undefined -DDEBUG`

## Testing Requirements

### Unit Tests

- Written in C, one test file per module (e.g., `test_tokenizer.c` tests `tokenizer.c`)
- Each test function: `void test_<module>_<behavior>(void)`
- Use a simple assertion macro (define in `tests/test.h`)
- Test happy path + error path + boundary cases
- Naming: `test_tokenizer_handles_empty_input`, `test_parser_rejects_double_pipe`

### Integration Tests

- Shell scripts in `tests/integration/`
- Each test: run a command in splash, capture output, compare to expected
- Test every feature end-to-end
- Format: `test_<milestone>_<feature>.sh`

### Fuzz Tests

- Fuzz the tokenizer and parser with random/malformed input
- No crashes, no hangs, no undefined behavior
- Use AFL or libFuzzer harnesses in `tests/fuzz/`

### Testing Workflow

- No feature is "done" until it has at least one test
- All tests must pass under sanitizers before committing
- Run `make test` before every commit

## Git Workflow

- **Branching**: Feature branches off `main` (e.g., `feat/tokenizer`, `feat/pipes`, `fix/fd-leak`)
- **Commits**: Conventional Commits format — `feat:`, `fix:`, `chore:`, `refactor:`, `test:`, `docs:`
- **Commit message**: Reference SPEC feature ID. Example: `feat: 2.7 pipe support for multi-stage pipelines`
- **Atomic commits**: One logical change per commit. A feature + its tests = one commit.
- **Pre-commit**: `make test` must pass. Never commit broken code.
- **No Co-Authored-By**: Never include co-author attributions in commits.
- Don't commit build artifacts, `.DS_Store`, or editor config files.

## Development Workflow

Every feature follows this process:

### Step 1: Design

- Present what will be implemented: the feature from SPEC.md, approach, affected files, edge cases.
- **Checkpoint**: Wait for user approval before writing any code.

### Step 2: Implement

- Write the code following all Coding Standards.
- Keep changes minimal and focused on the approved design.

### Step 3: Test & Verify

- Write unit tests and/or integration tests for the new feature.
- Run `make test` (and `make debug` + test under sanitizers).
- Fix any failures, leaks, or warnings.
- **Checkpoint**: Present test results to user for manual verification.

### Step 4: Document

- Write or update a design & implementation doc in `docs/features/`.
- One doc per milestone or major subsystem (e.g., `docs/features/tokenizer.md`, `docs/features/pipes-and-redirection.md`, `docs/features/job-control.md`).
- Each doc should cover:
  - **Design**: Why this approach was chosen. What alternatives were considered.
  - **Implementation**: How it works internally. Key data structures, control flow, fd/process lifecycle diagrams (ASCII is fine).
  - **Edge cases**: What tricky cases were handled and how.
  - **Testing**: What the tests cover and any known gaps.
- Keep docs concise but thorough — a future reader (or future session) should be able to understand the subsystem without reading every line of code.
- Update existing docs when modifying a feature — stale docs are worse than no docs.

### Step 5: Commit & Push

- After user confirms manual verification passes:
  - Commit with conventional commit message referencing SPEC feature ID
  - Include docs and test files in the same commit as the feature
  - Push to GitHub
  - Update SPEC.md: mark feature `[x]`, update "Last completed feature" and "Current milestone"

### Fast Track

For trivial changes (typo fixes, comment updates, Makefile tweaks): implement → verify build → commit. No design checkpoint needed.

### Response Template

For feature work, structure responses using this format:

```
## DESIGN
[Feature from SPEC, approach, affected files, edge cases]
→ CHECKPOINT: "Approve this approach?"

## IMPLEMENTATION
[Code changes — files modified/created]

## VERIFICATION
[Build result, test results, sanitizer output]
→ CHECKPOINT: "Please manually verify. Does it work?"

## DOCUMENTATION
[Design doc written/updated in docs/features/, what it covers]

## COMMIT
[Commit message, SPEC.md update, docs included]
```

## Session Workflow

Each session follows this pattern:

1. **Read SPEC.md** — Check "Last completed feature" and "Current milestone" to know where we left off
2. **Identify next feature** — Pick the next unchecked item in the current milestone
3. **Follow Development Workflow** — Design → Implement → Test → Document → Commit
4. **Update SPEC.md** — Mark completed features, update status fields
5. **Session Continuity Advisory** — Before transitioning to next feature, assess:
   ```
   Session Check:
   - Context used: [low / moderate / heavy]
   - Next feature relation: [closely related / loosely related / unrelated]
   - Recommendation: [continue this session / start a new session]
   - Reason: [1-sentence explanation]
   ```

## Approval Gates

- **Never write code without design approval.** Present the approach and wait for explicit confirmation.
- **Present options when ambiguous**: "Option A: [description] vs Option B: [description]. Which do you prefer?"
- **Wait for explicit approval** ("go ahead", "use option A", "looks good", etc.) before writing any code.
- If requirements are ambiguous, ask — don't assume.

## Debugging Protocol

- **Diagnose first**: Identify the root cause before proposing fixes. Read relevant code, check sanitizer output, reproduce the issue.
- **Present fix options**: Explain approaches with trade-offs when the fix is non-trivial.
- **Wait for approval**: Never auto-edit files on bug reports. Get confirmation before applying changes.
- **Verify the fix**: After applying, run `make test` and sanitizers to confirm no regressions.

## What NOT To Do

- Don't add features not in the spec without discussion.
- Don't over-engineer. Build the current milestone, not a future milestone.
- Don't skip error checking "for now" — check every syscall from day one.
- Don't use external libraries. Pure C standard library + POSIX + macOS APIs only.
- Don't cast `malloc` return values (this is C, not C++).
- Don't use `gets()`, `sprintf()`, `strcpy()` — use their safe counterparts (`fgets`, `snprintf`, `strncpy` or `strlcpy`).
- Don't call `exit()` in child processes after `fork()` — use `_exit()`.
- Don't use `signal()` — use `sigaction()` (portable, reliable behavior).
- Don't ignore compiler warnings — they are errors (`-Werror`).
- Don't commit code that leaks under sanitizers.

## How to Read SPEC.md

- **Status fields at top**: "Current milestone", "Last completed feature", "Last updated" — read these first to resume
- **Feature Log**: Checkboxes per feature — `[ ]` not started, `[~]` in progress, `[x]` done, `[!]` blocked
- **Verification sections**: Each milestone ends with verification criteria
- **Design Notes & Decisions Log**: Table at bottom with rationale for architectural choices
- Work through milestones in order (1 → 2 → 3 → ...)
- Within a milestone, features are ordered by dependency — do them in order

### Updating SPEC.md After Completing a Feature

1. Mark the feature checkbox: `[ ]` → `[x]`
2. Update "Last completed feature" at the top
3. Update "Current milestone" if the milestone is complete
4. Update "Last updated" date
5. Add any notable decisions to the Design Notes table
