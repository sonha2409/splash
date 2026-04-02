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
- `config_init()` — resolves the config directory path and creates it via `mkdir(path, 0755)`. Called once from `main()` before config file sourcing.
- `config_get_dir()` — returns the resolved path (or NULL on failure). Used by `main.c` to locate `init.sh`.
- `config_reset()` — clears state for unit testing.

### Edge Cases

- `$HOME` unset and `$XDG_CONFIG_HOME` unset: warns to stderr, shell continues without config directory
- Empty `$XDG_CONFIG_HOME`: treated as unset per XDG spec
- Path exceeds buffer: warns and skips (no truncated paths)
- Permission denied on mkdir: warns but doesn't abort the shell
- Directory already exists: `EEXIST` from `mkdir()` is handled silently

### Integration with main.c

`main.c` calls `config_init()` early, then uses `config_get_dir()` to build the path to `init.sh` instead of hardcoding `$HOME/.config/splash/`. The `~/.shellrc` sourcing remains HOME-based (it's a compatibility feature, not XDG).

### Testing

Unit tests in `tests/test_config.c` (17 assertions):
- Creates directory under `$HOME/.config/splash/`
- Respects `$XDG_CONFIG_HOME`
- Empty `$XDG_CONFIG_HOME` falls back to `$HOME`
- Returns NULL when neither env var is set
- Idempotent when directory already exists

All tests use temporary directories and restore environment variables after completion.
