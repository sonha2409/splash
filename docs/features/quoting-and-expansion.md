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

## Architecture

The expansion is integrated into `tokenizer.c`'s `read_word()` rather than being a separate pass. This is because the tokenizer already tracks quoting context (single-quoted, double-quoted, unquoted), which determines whether expansion should happen. Helper functions in `expand.c` do the actual lookups.

## Testing

- **Integration tests** in `tests/integration/test_m5_quoting_expansion.sh` — 22 tests
- Covers: double/single quote behavior, backslash escapes, `$VAR`/`${VAR}` expansion, special variables `$?`/`$$`, tilde expansion, quoting suppression of expansion
