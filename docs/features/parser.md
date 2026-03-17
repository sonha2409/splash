# Parser & Command Data Structures

## Design

Recursive descent parser that consumes a `TokenList` and produces a `Pipeline` AST. Chosen over table-driven or generated parsers for better error messages, easier incremental parsing support, and straightforward extensibility.

### Grammar (Milestone 1)

```
pipeline  = command (PIPE command)* [BACKGROUND]
command   = WORD+
```

Future milestones will extend this with redirections (M2), `&&`/`||`/`;` (M8), and control structures (M8).

## Implementation

### AST Data Structures

```
Pipeline
├── commands[]  → SimpleCommand*  (array of pointers, owned)
├── num_commands
├── cmd_capacity
└── background  → int (0 or 1)

SimpleCommand
├── argv[]      → char*  (NULL-terminated, each string owned)
├── argc
└── argv_capacity
```

Both structures track capacity explicitly for clean growth via `xrealloc`.

### Parser Algorithm

1. Skip leading newlines
2. Check for empty input (EOF) or incomplete input → return NULL silently
3. Check for leading operator → syntax error
4. Parse first simple command (consume consecutive WORD tokens)
5. While next token is PIPE: consume pipe, parse next simple command
6. Check for BACKGROUND token → set flag
7. Verify remaining tokens are EOF/NEWLINE → syntax error if not
8. Handle INCOMPLETE tokens at any point → return NULL silently (for incremental parsing)

### Error Handling

- Parse errors print to stderr in format: `splash: syntax error: <message>`
- Incomplete input (unterminated quotes) returns NULL without error — this is expected during interactive editing
- All error paths free partially-built AST nodes (no leaks)

### Memory Ownership

- `parser_parse()` returns a heap-allocated `Pipeline`. Caller owns it.
- `Pipeline` owns its `SimpleCommand` array and all commands in it.
- `SimpleCommand` owns its `argv` array and all strings in it.
- `pipeline_free()` recursively frees everything.
- On parse error, the parser frees any partially-built structures before returning NULL.

## Edge Cases

| Input | Result | Rationale |
|-------|--------|-----------|
| `""` | NULL, no error | Empty input is not an error |
| `"| ls"` | NULL + error | Pipe with no left-hand side |
| `"ls |"` | NULL + error | Pipe with no right-hand side |
| `"ls | | grep"` | NULL + error | Missing command between pipes |
| `"echo \"unterm"` | NULL, no error | Incomplete input, not a syntax error |
| `"sleep 10 &"` | Pipeline with background=1 | Background flag parsed |

## Testing

12 test functions, 40 assertions covering:
- Empty input, single command, command with args
- 2-stage and 3-stage pipelines
- Background execution
- Error cases: pipe at start, end, double pipe
- Incomplete input (unterminated quote)
- Quoted arguments preserved as single tokens
- Absolute path commands

All passing under ASan + UBSan.
