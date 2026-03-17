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

## History Navigation (6.5)

### Design

Up/Down arrows browse command history. The editor tracks a `hist_index` that starts at `history_count()` (one past the end, representing "current input"). Up decrements, Down increments.

When the user first presses Up, the current typed text is saved in `saved_line`. Pressing Down back to the bottom restores it. This matches bash/zsh behavior.

### Persistent History

History is persisted to `~/.config/splash/history` (one command per line). The directory is created automatically if missing.

- `history_load()`: Called during `history_init()`. Reads the file line by line.
- `history_append()`: Called from `history_add()`. Appends a single line (avoids rewriting the whole file).
- Dedup: consecutive duplicates are skipped both in-memory and on disk.
- Max entries: 1000. When full, oldest entries are dropped from memory (file grows unbounded but is only loaded up to HISTORY_MAX on startup).

### Edge Cases

- No history file → start fresh, no error.
- `$HOME` not set → persistence disabled, in-memory history still works.
- Up at top of history → no-op.
- Down past bottom → restores saved current input.
- `saved_line` freed on all exit paths (Enter, Ctrl-C, Ctrl-D, EOF, realloc failure).

## Reverse Incremental Search (6.6)

### Design

Ctrl-R enters a separate input loop (`do_reverse_search()`) within `editor_readline()`. The search prompt displays `(reverse-i-search)'query': matched_line`, matching bash's format.

### How it works

- Each keystroke appends to the query and searches backwards through history via `strstr()` substring matching.
- Ctrl-R again while in search mode jumps to the next older match.
- Backspace shrinks the query and re-searches from the end.
- Enter accepts the match — the matched line is shown on the normal prompt and immediately submitted.
- Escape cancels — returns to normal editing with the buffer unchanged.
- Ctrl-C aborts — discards the line entirely.

### Key implementation details

- `find_match(query, qlen, start_idx)`: Linear scan backwards from `start_idx`, returns first index where `strstr(entry, query)` succeeds.
- Empty query matches the most recent history entry.
- Escape handling: uses a short `VTIME` timeout (100ms) to distinguish bare Escape from escape sequences (arrow keys, etc.), consuming and discarding any trailing bytes.

### Edge Cases

- No matches for query → display stays on last match (or empty).
- Ctrl-R at oldest match → no-op (doesn't wrap).
- Empty history → search shows nothing, Enter returns NULL (no match to accept).

## Autosuggestions (6.7)

### Design

As the user types, the most recent history entry matching the current input as a prefix is shown as greyed-out text after the cursor. Right-arrow, End, or Ctrl-E at end of line accepts the suggestion, filling the buffer with the full entry.

### How it works

- `find_suggestion(buf, len)`: Linear scan backwards through history, finds the first entry where `strncmp(entry, buf, len) == 0` and entry is longer than the current input.
- Called on every `refresh_line()` invocation — the suggestion suffix is rendered in dim grey (`\x1b[2;37m`).
- Right-arrow/End/Ctrl-E at end of line: if a suggestion exists, replaces the buffer with the full suggestion text.
- Tab is intentionally **not** used for autosuggestions — it's reserved for filesystem tab completion (6.9–6.10). This matches fish shell's UX.

### Edge Cases

- No matching history entry → no grey text shown.
- Cursor not at end of line → Right-arrow moves cursor normally, no suggestion acceptance.
- After accepting suggestion → no further suggestion shown (exact match, not longer).

## Future work (later features in M6)

- 6.8: Syntax highlighting — will tokenize buffer and emit ANSI codes in `refresh_line()`.
- 6.9–6.10: Tab completion — will intercept Tab key in the main read loop.
