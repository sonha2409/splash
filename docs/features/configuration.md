# Configuration System

## Overview

The configuration system provides XDG-compliant directory setup and config file sourcing for splash. It creates `~/.config/splash/` (or `$XDG_CONFIG_HOME/splash/`) on startup and sources user config files.

## 9.1 XDG Directory Setup

### Design

Follows the [XDG Base Directory Specification](https://specifications.freedesktop.org/basedir-spec/latest/):

1. If `$XDG_CONFIG_HOME` is set and non-empty, use `$XDG_CONFIG_HOME/splash/`
2. Otherwise, default to `$HOME/.config/splash/`
3. Create intermediate directories as needed (`~/.config/` then `~/.config/splash/`)
4. If directory already exists, silently continue (idempotent)

### Implementation

- **Module**: `src/config.c` / `src/config.h`
- **Static state**: `config_dir[1024]` stores the resolved path, `config_dir_valid` tracks initialization success
- `config_init()` — resolves the config directory path and creates it via `mkdir(path, 0755)`. Called once from `main()` **only when `isatty(0)` is true**. Non-interactive splash (piped scripts, integration tests) deliberately does not create files in `$HOME` / `$XDG_CONFIG_HOME` so that running splash in a pipeline never pollutes the user's environment. The XDG dir is only ever materialized by an interactive session.
- `config_get_dir()` — returns the resolved path (or NULL on failure). Used by `main.c` to locate `init.sh`.
- `config_reset()` — clears state for unit testing.

### Edge Cases

- `$HOME` unset and `$XDG_CONFIG_HOME` unset: warns to stderr, shell continues without config directory
- Empty `$XDG_CONFIG_HOME`: treated as unset per XDG spec
- Path exceeds buffer: warns and skips (no truncated paths)
- Permission denied on mkdir: warns but doesn't abort the shell
- Directory already exists: `EEXIST` from `mkdir()` is handled silently

### Integration with main.c

`main.c` calls `config_init()` and `config_load()` from inside the `if (interactive)` block (alongside `editor_init()` and the `init.sh` / `~/.shellrc` sourcing), then uses `config_get_dir()` to build the path to `init.sh` instead of hardcoding `$HOME/.config/splash/`. The `~/.shellrc` sourcing remains HOME-based (it's a compatibility feature, not XDG).

### Testing

Unit tests in `tests/test_config.c` (17 assertions):
- Creates directory under `$HOME/.config/splash/`
- Respects `$XDG_CONFIG_HOME`
- Empty `$XDG_CONFIG_HOME` falls back to `$HOME`
- Returns NULL when neither env var is set
- Idempotent when directory already exists

All tests use temporary directories and restore environment variables after completion.

## 9.2 config.toml Parsing

### Design

A minimal hand-rolled TOML parser — no external dependencies. Supports only what splash needs:

- **Sections**: `[section_name]` headers
- **Key-value pairs**: `key = value`
- **Value types**: quoted strings (double or single), integers, booleans (`true`/`false`), bare strings
- **Comments**: lines starting with `#`, inline `# comment` after values
- **Escape sequences** in double-quoted strings: `\n`, `\t`, `\\`, `\"`
- No nested tables, arrays, or inline tables

### Implementation

- **Storage**: flat array of `ConfigEntry` structs (key-value string pairs), max 256 entries
- Keys are stored in `section.name` format (e.g., `"prompt.format"`, `"history.max_size"`)
- Duplicate keys: last value wins (silently overwritten)

API:
- `config_load()` — parses `config.toml` from the config directory; silently skips if file doesn't exist
- `config_load_from(path)` — parses from an explicit path (used by tests)
- `config_get_string(key)` — returns value or NULL
- `config_get_int(key, default)` — parses as integer, returns default on failure
- `config_get_bool(key, default)` — recognizes true/false case-insensitively

### Edge Cases

- Missing file: silently skipped
- Malformed section header (no `]`): warned, line skipped
- Missing `=`: warned, line skipped
- Empty key: warned, line skipped
- Unknown sections: accepted and stored (forward-compatible)
- Invalid int (e.g., `"hello"` for `config_get_int`): returns default value

### Testing

Unit tests in `tests/test_config.c` (27 new assertions):
- Basic section/key/value parsing with strings, ints, bools
- Single-quoted literal strings
- Bare (unquoted) values
- Inline comments stripped
- Escape sequences in double-quoted strings
- Duplicate keys (last wins)
- Missing key returns default
- Invalid int returns default
- Bool case-insensitive matching

## 9.3 init.sh

### Design

On interactive startup, splash sources `$CONFIG_DIR/init.sh` if it exists. This allows users to define aliases, functions, and environment variables that persist for the session.

### Implementation

- `main.c` calls `source_if_exists(config_dir + "/init.sh")` after `config_init()` and `config_load()`, inside the `if (interactive)` block
- Uses the existing `source` builtin which reads the file line by line and executes each line via `executor_execute_line()`
- Only runs for interactive sessions (piped/scripted input skips sourcing, as is standard shell behavior)

### Testing

Integration tests in `tests/integration/test_m9_config.sh`:
- Setting env vars via `export` in init.sh
- Defining aliases in init.sh
- Defining functions in init.sh

## 9.4 ~/.shellrc Compatibility

### Design

After sourcing `init.sh`, splash also sources `~/.shellrc` if it exists. This provides a familiar location for users migrating from other shells.

### Implementation

- `main.c` sources `$HOME/.shellrc` after init.sh, inside the `if (interactive)` block
- Uses `$HOME` directly (not the XDG config dir — this is a compatibility path)

### Testing

Integration test verifies env vars set in `.shellrc` are available after sourcing.

## 9.5 Variable Prompt

### Design

The prompt is rebuilt before every `editor_readline()` call, supporting dynamic content like the current directory and git branch.

**Priority order**:
1. `$PROMPT` environment variable
2. `prompt.format` in `config.toml`
3. Default: `"splash> "`

### Escape Sequences

| Escape | Expansion |
|--------|-----------|
| `\u` | Username (`$USER`) |
| `\h` | Short hostname (up to first `.`) |
| `\w` | Current working directory (`~` for `$HOME`) |
| `\W` | Basename of current working directory |
| `\$` | `#` if root, `$` otherwise |
| `\e` | ESC character (for ANSI color codes) |
| `\g` | Current git branch (or short hash if detached) |
| `\\` | Literal backslash |

### Implementation

- `config_expand_prompt(format)` — processes escape sequences, returns newly allocated string
- `config_build_prompt()` — checks env/config/default priority, calls expand
- `get_git_branch()` — walks up from cwd looking for `.git/HEAD`, parses `ref: refs/heads/...`
- `buf_append()` — helper for dynamic string building

### Example config.toml

```toml
[prompt]
format = "\e[32m\u@\h\e[0m \e[34m\w\e[0m (\g) \$ "
```

### Testing

Unit tests (15 assertions):
- Literal passthrough, user expansion, cwd basename, `$` sign
- Backslash escaping, ESC character
- Git branch detection, home directory `~` substitution
- `config_build_prompt()` with default, env var, and config.toml

## 9.6 ON_ERROR Env Var

### Design

When a command exits with a non-zero status in interactive mode, if the `ON_ERROR` environment variable is set and non-empty, its value is printed to stderr.

### Implementation

A 4-line check in `main.c` after `executor_execute_line()`:
```c
if (last_status != 0 && interactive) {
    const char *on_error = getenv("ON_ERROR");
    if (on_error && on_error[0] != '\0') {
        fprintf(stderr, "%s\n", on_error);
    }
}
```

### Usage

```sh
export ON_ERROR="Command failed!"
# or with color:
export ON_ERROR=$'\e[31m✗ Command failed\e[0m'
```

### Edge Cases

- Only in interactive mode (non-interactive scripts don't trigger it)
- Empty `ON_ERROR` is treated as unset (no message)
- Output goes to stderr to avoid interfering with pipelines
