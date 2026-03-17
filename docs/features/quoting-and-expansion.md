# Quoting, Escaping, and Expansions

## Overview

Splash processes input through several expansion phases, all integrated into the tokenizer's `read_word()` function. This keeps the quoting context available for deciding what to expand.

## Quoting (5.1–5.3)

### Double Quotes
- Content is a single word (spaces preserved)
- Variable expansion (`$VAR`, `${VAR}`) happens inside
- Backslash escapes: `\\`, `\"`, `\$`, `\`` , `\n`, `\t`
- Tilde does NOT expand inside double quotes

### Single Quotes
- Everything is literal — no expansion of any kind
- Cannot contain a single quote (even escaped)

### Backslash Escaping
- Outside quotes: escapes the next character (makes it literal)
- `\n` → newline, `\t` → tab (in both unquoted and double-quoted contexts)
- `\\` → literal backslash

## Variable Expansion (5.4–5.5)

Expansion happens inline during tokenization in `read_word()`. When `$` is encountered in unquoted or double-quoted context:

### Forms
- `$VAR` — alphanumeric + underscore name
- `${VAR}` — braced form, allows adjacent text: `${VAR}suffix`
- Undefined variables expand to empty string

### Special Variables
Tracked in `expand.c` via setter functions called from the executor:
- `$?` — exit status of last foreground command
- `$$` — PID of the shell process
- `$!` — PID of last background job
- `$_` — last argument of the previous command

## Tilde Expansion (5.6)

Only at word start in unquoted context:
- `~` → `$HOME`
- `~/path` → `$HOME/path`
- `~user` → user's home directory (via `getpwnam()`)
- Does NOT expand inside any quotes

## Wildcarding / Glob Expansion (5.7)

Expands unquoted `*` and `?` in command arguments to matching filenames.

### Sentinel Byte Approach

The key challenge is distinguishing unquoted `*` from quoted `"*"` after tokenization. The solution uses sentinel bytes:

1. **Tokenizer** (`read_word()`): When `*` or `?` appears in unquoted context, it's stored as `\x01` or `\x02` (sentinel bytes) in the token value. Inside quotes, they remain literal characters.
2. **Executor** (`executor_execute_line()`): After parsing, calls `expand_glob_argv()` on each command. This scans argv for sentinels, performs glob expansion, and replaces the single arg with sorted matches.
3. **No match**: If a glob pattern matches nothing, sentinels are unescaped back to literal `*`/`?` and the word is kept as-is (standard shell behavior).

### Pattern Matching

- `*` — matches zero or more characters (any sequence)
- `?` — matches exactly one character
- Path support: `src/*.c` splits on last `/`, opens that directory, matches filenames
- Hidden files (`.` prefix) are excluded unless the pattern explicitly starts with `.`
- Results are sorted alphabetically

### What Doesn't Expand

- `"*.c"` — double-quoted glob characters remain literal
- `'*.c'` — single-quoted glob characters remain literal
- `\*` — backslash-escaped glob characters remain literal

## Architecture

The expansion pipeline is split across two phases:

1. **Tokenizer phase** (`tokenizer.c`): Handles quoting context, variable expansion, tilde expansion, and marks unquoted glob chars with sentinels. This is integrated into `read_word()` because it needs quoting context.
2. **Executor phase** (`executor.c`→`expand.c`): Performs glob expansion on parsed argv arrays. This must happen after parsing because glob expansion can produce multiple arguments from a single token.

Helper functions in `expand.c` do the actual lookups and filesystem operations.

## Testing

- **Unit tests** in `tests/test_expand.c` — 70 tests covering `expand_has_glob()`, `expand_glob_unescape()`, `expand_glob()` with star, question mark, hidden files, no-match, path patterns
- **Integration tests** in `tests/integration/test_m5_quoting_expansion.sh` — 22 tests for quoting/variables/tilde
- **Integration tests** in `tests/integration/test_m5_wildcarding.sh` — 11 tests for glob expansion, hidden file exclusion, quoted literal preservation, no-match fallback
