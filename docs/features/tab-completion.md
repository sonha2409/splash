# Tab Completion — Paths & Commands (Features 6.9, 6.10)

## Design

Tab completion for file and directory paths, with common-prefix disambiguation and double-tab listing of all candidates.

### Behavior

1. **Single match** — replace the word with the full match. Append `/` for directories, ` ` for files.
2. **Multiple matches** — complete to the longest common prefix.
3. **Double-Tab** (consecutive) — list all matches in columns below the prompt, then redraw.
4. **No matches** — do nothing.

### Word extraction

The current word is identified by scanning backwards from the cursor position until hitting a delimiter: space, tab, or shell operator (`|`, `;`, `&`, `>`, `<`, `(`, `)`).

## Implementation

### Files

- `src/complete.h` — Public API: `CompletionResult`, `complete_path()`, `completion_common_prefix()`, `completion_result_free()`
- `src/complete.c` — Path completion engine using `opendir()`/`readdir()`
- `src/editor.c` — Tab key handling (character 9) with double-tab state tracking

### `complete_path()` algorithm

1. Split the prefix into directory part and filename part at the last `/`
2. Handle `~` prefix by expanding to `$HOME` for the `opendir()` call while preserving `~/` in results
3. Open the directory and iterate entries with `readdir()`
4. Filter: entry name must start with the filename prefix
5. Skip `.` and `..` always; skip other dotfiles unless prefix starts with `.`
6. `stat()` each match to determine if it's a directory (append `/`)
7. Sort results alphabetically with `qsort()`

### `completion_common_prefix()`

Compares all matches character by character to find the longest shared prefix. Used when multiple matches exist to extend the word as far as possible.

### Editor integration

- `last_was_tab` flag tracks consecutive Tab presses
- Reset to 0 on any non-Tab keypress
- First Tab: complete or extend to common prefix
- Second Tab: print all matches in columns (assuming 80-char width), then redraw prompt

### Buffer manipulation

When replacing a word with a completion:
1. Calculate new buffer size
2. Grow buffer if needed (`realloc`)
3. Shift text after cursor to make room
4. Copy completion into the word position
5. Update `len` and `pos`

## Edge Cases

- **Empty prefix** — lists all non-hidden files in current directory
- **Nonexistent directory** — returns empty result (no crash)
- **Hidden files** — only shown when prefix starts with `.`; `.` and `..` always excluded
- **Directory completion** — trailing `/` appended, no trailing space (allows continuing to type)
- **Tilde paths** — `~/Do` expands `~` for lookup but preserves `~/` in the result

## Command Completion (Feature 6.10)

When the word being completed is in command position (first word on the line, or first word after `|`, `||`, `&&`, `;`, `&`, `(`), Tab completes from:

1. **Shell builtins** — `cd`, `exit`, `export`, `fg`, `bg`, `jobs`, `alias`, etc.
2. **User-defined aliases** — iterated via `alias_count()` / `alias_get_name()`
3. **$PATH executables** — each directory in `$PATH` is scanned with `opendir()`/`readdir()`, entries checked with `access(X_OK)`

Deduplication ensures a name appearing in multiple sources (e.g., `echo` as both a PATH executable and potential alias) only appears once.

If the prefix contains a `/`, it's treated as a path and delegated to `complete_path()` instead.

### Command position detection

In `editor.c`, the Tab handler determines command position by scanning backwards from the word start, skipping whitespace, and checking if the previous non-whitespace character is an operator (`|`, `;`, `&`, `(`) or if we're at the beginning of the line.

## Testing

`tests/test_complete.c` — 153 assertions covering:

**Path completion:**
- Empty prefix returns results
- Nonexistent directory returns empty
- No-match prefix returns empty
- Known directory contents (src/)
- Partial filename matching
- Common prefix computation
- Directory trailing slash
- Hidden file filtering
- Alphabetical sort order
- Single match common prefix
- No-match common prefix returns NULL

**Command completion:**
- Builtin matching (exact and partial)
- PATH executable matching
- No-match returns empty
- Results sorted alphabetically
- No duplicate entries
- Prefix with `/` delegates to path completion
