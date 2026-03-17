# Line Editor

## Overview

The line editor (`src/editor.c`) provides fish-style interactive line editing by switching the terminal from canonical (line-buffered) mode to raw mode using `termios`. This allows splash to read individual keypresses and provide real-time editing features.

## Design

### Why raw mode?

In canonical mode, the terminal driver buffers input until Enter is pressed. The shell only sees complete lines. This makes it impossible to:
- Move the cursor within a line
- Provide syntax highlighting on each keystroke
- Show autosuggestions
- Handle Tab completion

Raw mode gives us full control over input processing at the cost of having to handle everything ourselves (echo, line editing, signal keys).

### Alternatives considered

- **readline/libedit**: External dependencies, against project rules (pure C, no external libs).
- **ncurses**: Heavy dependency for what we need. Also external.
- **Custom termios**: Chosen. Minimal, full control, no dependencies.

## Implementation

### Terminal mode management

- `editor_init()`: Saves original `termios` via `tcgetattr()`, registers `editor_cleanup()` with `atexit()`.
- `enter_raw_mode()`: Applies raw settings before each line read. Disables `ICANON`, `ECHO`, `ISIG`, `IEXTEN`, `IXON`, `OPOST`. Sets `VMIN=1`, `VTIME=0`.
- `leave_raw_mode()`: Restores original `termios` after each line. Idempotent.
- `editor_cleanup()`: Called on exit/signals to ensure terminal is always restored.

Raw mode is entered/exited per-line rather than staying in raw mode permanently. This ensures child processes (executed commands) get a normal terminal.

### Line buffer

- Dynamically allocated, starts at 256 bytes, doubles as needed.
- Tracks both `len` (text length) and `pos` (cursor position) separately to support mid-line editing.
- Returned to caller as a malloc'd string (ownership transfer).

### Display refresh

`refresh_line()` redraws the entire prompt + buffer on each edit:
1. Carriage return (move to column 0)
2. Write prompt
3. Write buffer contents
4. Erase to end of line (`ESC[0K`)
5. Position cursor at `prompt_len + pos`

This full-redraw approach is simple and correct. It will also work well when syntax highlighting is added later (6.8) — we just need to emit ANSI color codes during step 3.

### Non-interactive fallback

When stdin is not a TTY (piped input, scripts), `editor_readline()` falls back to `fgets()` — no raw mode, no prompt display. This keeps `echo "cmd" | splash` working.

## Keybindings (6.1 scope)

| Key | Action |
|-----|--------|
| Printable chars | Insert at cursor |
| Enter | Submit line |
| Backspace / Ctrl-H | Delete char before cursor |
| Delete (ESC[3~) | Delete char under cursor |
| Ctrl-D | Delete under cursor, or EOF on empty line |
| Left / Right | Move cursor |
| Home / Ctrl-A | Move to start of line |
| End / Ctrl-E | Move to end of line |
| Ctrl-K | Kill to end of line |
| Ctrl-U | Kill to start of line |
| Ctrl-L | Clear screen |
| Ctrl-C | Discard line, fresh prompt |
| Up / Down | (Placeholder for history navigation) |

## Edge Cases

- **stdin not a TTY**: Detected via `isatty()`, falls back to `fgets()`.
- **tcgetattr/tcsetattr failure**: Falls back to `fgets()` for that line.
- **Signal during raw mode**: `atexit()` handler restores terminal.
- **Double cleanup**: `leave_raw_mode()` is idempotent (checks `raw_mode_enabled` flag).
- **Buffer growth**: `realloc()` checked for NULL, error reported, line aborted.

## Testing

- Non-interactive mode tested via piped input: `echo "echo hello" | ./splash`
- Interactive mode tested manually: typing, backspace, cursor movement, Ctrl keys, Ctrl-C, Ctrl-D EOF.
- All existing unit tests pass (no regressions).
- Builds clean under `-fsanitize=address,undefined`.

## Future work (later features in M6)

- 6.2–6.4: Already partially implemented (basic char input, cursor, editing keys).
- 6.5: History navigation (Up/Down arrows) — placeholders in escape sequence handler.
- 6.7: Autosuggestions — will hook into `refresh_line()` to append greyed text.
- 6.8: Syntax highlighting — will tokenize buffer and emit ANSI codes in `refresh_line()`.
- 6.9–6.10: Tab completion — will intercept Tab key in the main read loop.
