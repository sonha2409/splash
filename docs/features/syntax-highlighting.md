# Syntax Highlighting (Feature 6.8)

## Design

Splash highlights the command line on every keystroke using ANSI color codes. The goal is fish-shell-style instant feedback: valid commands appear green, invalid ones red, strings yellow, etc.

### Why a separate scanner?

The existing tokenizer (`tokenizer.c`) performs variable expansion, tilde expansion, and command substitution during tokenization. This is unsuitable for highlighting because:

1. We cannot run `$()` command substitutions on every keystroke
2. We need the original character positions in the raw input for coloring
3. Expansion can change string lengths, breaking position mapping

So `highlight.c` implements a lightweight scanner that identifies syntactic categories without expanding anything.

### Color scheme

| Category | Color | ANSI Code | Example |
|----------|-------|-----------|---------|
| Valid command | Green | `\033[32m` | `ls`, `echo`, `cd` |
| Invalid command | Red | `\033[31m` | `xyznotfound` |
| Quoted string | Yellow | `\033[33m` | `"hello"`, `'world'` |
| Operator | Cyan | `\033[36m` | `\|`, `>`, `&&`, `\|\|`, `;` |
| Variable | Magenta | `\033[35m` | `$HOME`, `${VAR}`, `$(cmd)` |
| Comment | Grey | `\033[90m` | `# comment` |
| Arguments | Default | `\033[0m` | `-la`, `/tmp` |

### Command position tracking

The scanner tracks whether the next word is in "command position" — meaning it should be validated as a command name. Command position is true:

- At the start of input
- After `|`, `||`, `&&`, `;`, `&`

Command position is false after redirect operators (`>`, `<`, `>>`, etc.) since the next word is a filename.

### Command validation

A word in command position is checked against:

1. Shell builtins (`builtin_is_builtin()`)
2. Absolute/relative paths (`access(path, X_OK)`)
3. `$PATH` directories (iterating colon-separated dirs with `access()`)

## Implementation

### Files

- `src/highlight.h` — Public API: `HighlightType` enum and `highlight_line()` function
- `src/highlight.c` — Lightweight scanner and command validation
- `src/editor.c` — Integration: `refresh_line()` calls `highlight_line()` and emits colored output

### Data flow

```
User types a character
    → editor updates buffer
    → refresh_line() called
    → highlight_line(buf, len) → HighlightType colors[len]
    → write_highlighted() emits ANSI-colored spans
    → autosuggestion appended in dim grey
    → cursor repositioned
```

### `highlight_line()` scanner

Walks input character by character:

1. **Whitespace** — skip, preserve `cmd_pos` state
2. **`#` at word boundary** — fill rest of line with `HL_COMMENT`
3. **`2>` at word boundary** — mark as `HL_OPERATOR`
4. **Operator chars** (`|`, `>`, `<`, `&`, `;`, `(`, `)`) — mark span as `HL_OPERATOR`, update `cmd_pos`
5. **Single quote** — scan to closing `'`, mark all as `HL_STRING`
6. **Double quote** — scan to closing `"`, mark as `HL_STRING`, but `$VAR` inside gets `HL_VARIABLE`
7. **`$`** — scan variable name/braces/parens, mark as `HL_VARIABLE`
8. **Word** — read until delimiter, if `cmd_pos` check validity → `HL_COMMAND` or `HL_ERROR`

### Editor integration

`refresh_line()` was modified to:
1. Call `highlight_line()` to get a color array
2. Use `write_highlighted()` to emit the buffer with ANSI color transitions
3. Free the color array
4. Continue with autosuggestion rendering (dim grey) as before

`write_highlighted()` groups consecutive characters with the same color into spans, emitting ANSI escape sequences only at color transitions for efficiency.

## Edge Cases

- **Empty input** — returns a 1-element zeroed array, no colors emitted
- **Unclosed quotes** — highlighted as string up to end of input (no crash)
- **Unclosed `$(` or `${`** — highlighted as variable up to end of input
- **`#` inside a word** — not treated as comment (only at word boundary)
- **Backslash escapes** — consumed as part of the word, not misinterpreted
- **Autosuggestion text** — rendered after highlighting with its own dim grey style, unaffected by highlighting

## Testing

`tests/test_highlight.c` — 124 assertions covering:

- Empty input
- Valid/invalid command detection
- Command with arguments (args stay default color)
- Pipe resetting command position
- Single and double quoted strings
- Variables (unquoted, braced, command substitution)
- Variables inside double quotes
- Comments
- Redirects (next word is not command position)
- `&&`, `||`, `;` resetting command position
- Builtin commands
