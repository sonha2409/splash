# Tokenizer

## Design

The tokenizer is a hand-rolled lexer that converts an input string into a flat array of tokens. It was chosen over Flex/Bison for better error messages, easier incremental parsing support, and simpler integration with the syntax highlighter.

### Alternatives Considered

- **Flex**: Generates fast lexers but produces opaque code, harder to debug and extend. Doesn't naturally support incremental/partial tokenization.
- **Simple strtok-based splitting**: Too fragile for quoting, escapes, and multi-character operators.

## Implementation

### Token Types

23 token types covering:
- `TOKEN_WORD` — commands, arguments, filenames
- Pipe operators: `|`, `|>` (structured)
- Redirections: `>`, `>>`, `<`, `2>`, `>&`, `>>&`
- Control flow: `&&`, `||`, `;`, `&` (background)
- Grouping: `(`, `)`
- Substitution: `$(`, `` ` ``, `<(`, `>(`
- Special: `TOKEN_NEWLINE`, `TOKEN_EOF`, `TOKEN_INCOMPLETE`

### Data Structures

```c
Token:  { type, value (heap-allocated string), pos, length }
TokenList: { tokens (dynamic array), count, capacity }
```

Position and length fields track where each token sits in the original input — essential for syntax highlighting.

### Lexing Algorithm

Single-pass, character-by-character scan with 1-2 character lookahead:

1. Skip whitespace (except newline)
2. Check for multi-character operators first (longest match): `>>&`, `>>`, `>&`, `|>`, `||`, `&&`, `$(`, `<(`, `>(`
3. Then single-character operators: `|`, `>`, `<`, `&`, `;`, `(`, `)`, `` ` ``
4. Special case: `2>` only recognized at word boundary (not inside `file2>`)
5. Otherwise: read a word, handling quoting and escapes

### Quoting

- **Double quotes**: Content becomes a single WORD token. Backslash escapes `$`, `"`, `\`, `` ` ``, newline inside double quotes. Other backslashes are literal.
- **Single quotes**: Everything literal until closing quote. No escapes.
- **Backslash** (unquoted): Escapes the next character, joining it to the current word.
- **Unterminated quotes/escapes**: Produce `TOKEN_INCOMPLETE` for incremental parsing.

### Memory Ownership

- `tokenizer_tokenize()` returns a heap-allocated `TokenList`. Caller owns it.
- Each token's `value` is independently heap-allocated.
- `token_list_free()` frees everything (values + array + list).

## Edge Cases

| Input | Result | Rationale |
|-------|--------|-----------|
| `""` (empty) | `[EOF]` | No tokens to produce |
| `\|\|\|` | `[OR, PIPE]` | Longest match first |
| `file2>` | `[WORD("file2"), REDIRECT_OUT]` | `2>` only at word boundary |
| `echo "unterminated` | `[WORD, INCOMPLETE, EOF]` | Signals incomplete input |
| `echo \\` | `[WORD, INCOMPLETE, EOF]` | Trailing backslash needs continuation |

## Testing

25 test functions covering:
- Empty input, single/multiple words
- All operator types
- All redirect types including combined stderr
- Double quotes, single quotes, backslash escapes
- Unterminated quotes and trailing backslash (incomplete)
- Position tracking accuracy
- Edge cases: `|||`, `file2>`, adjacent operators, whitespace variations

85 total assertions, all passing under ASan + UBSan.
