# Scripting (Progressive POSIX)

Design and implementation notes for Milestone 8: scripting features.

## 8.1 Command Lists (`;`, `&&`, `||`)

### Design

Command lists allow multiple pipelines on a single line, with control flow based on exit codes:

- `cmd1 ; cmd2` — sequential execution, always runs both
- `cmd1 && cmd2` — AND: run `cmd2` only if `cmd1` succeeds (exit 0)
- `cmd1 || cmd2` — OR: run `cmd2` only if `cmd1` fails (exit != 0)

Operators are left-to-right with equal precedence (same as bash/POSIX). For example:
`false && echo a || echo b` — skips `echo a` (false &&), then runs `echo b` (|| after non-zero).

### Implementation

**New AST node**: `CommandList` in `command.h` wraps an array of `Pipeline*` connected by `ListOpType` operators. This is now the top-level AST returned by `parser_parse()`.

```
CommandList
├── pipelines[0]: Pipeline (ls | grep foo)
├── operators[0]: LIST_AND
├── pipelines[1]: Pipeline (echo done)
├── operators[1]: LIST_SEMI
└── pipelines[2]: Pipeline (echo always)
```

**Parser**: `parser_parse()` return type changed from `Pipeline*` to `CommandList*`. Internally, `parse_pipeline()` was extracted as a helper. After parsing each pipeline, the parser checks for `;`/`&&`/`||` and continues. Trailing `;` is valid (no empty pipeline created).

**Executor**: New `executor_execute_list()` iterates pipelines, evaluating operators against the previous exit status to decide whether to execute or skip each pipeline. `executor_execute_line()` now calls this instead of `executor_execute()` directly.

### Edge Cases

- **Trailing semicolon**: `cmd ;` — valid, parsed as single pipeline
- **Mixed operators**: `a && b || c ; d` — left-to-right, 4 pipelines, 3 operators
- **Pipes within lists**: `ls | grep foo ; echo done` — pipeline is fully parsed before list operator
- **Background**: `cmd1 ; cmd2 &` — only `cmd2` is backgrounded (background flag is per-pipeline)
- **Error: missing RHS**: `cmd &&` and `cmd ||` produce syntax errors; `cmd ;` does not

### Testing

- **Unit tests** (10 new in `test_parser.c`): semicolons, &&, ||, mixed operators, trailing semicolon, error cases
- **Integration tests** (16 in `test_m8_command_lists.sh`): end-to-end with real command execution, pipe interaction, chained operators

## 8.2 `if/elif/else/fi`

### Design

Conditional execution based on exit codes:

```
if condition; then body; fi
if condition; then body; else body; fi
if cond1; then body1; elif cond2; then body2; else body3; fi
```

Conditions and bodies are full command lists (can contain `;`, `&&`, `||`, pipes, nested if-blocks).

### Implementation

**AST refactor**: `CommandList` entries changed from `Pipeline**` to `Node*` — a tagged union (`NODE_PIPELINE` or `NODE_IF`). This makes compound commands first-class entries in command lists, extensible for `for`/`while`/`case` later.

New types in `command.h`:
- `NodeType` enum, `Node` tagged union
- `IfClause` (condition + body pair)
- `IfCommand` (array of clauses + optional else_body)

**Parser**: Refactored into three mutually recursive functions:
- `parse_command_list_until(stops)` — parses entries separated by `;`/`&&`/`||`, stopping at keyword stop words (e.g., `then`, `elif`, `else`, `fi`)
- `parse_entry()` — dispatches to `parse_if_command()` or `parse_pipeline()` based on keyword detection
- `parse_if_command()` — parses the full `if/then/elif/else/fi` structure

`parser_parse()` is now a thin wrapper around `parse_command_list_until(NULL, 0)`.

**Executor**: `execute_node()` dispatches on node type. `execute_if_command()` evaluates each clause's condition list; if exit code is 0, executes the body and returns. Falls through to else_body if no clause matched.

### Edge Cases

- **Nested if**: `if true; then if false; then a; else b; fi; fi` — inner `fi` correctly matched
- **Compound conditions**: `if true && false; then ...` — condition is a full command list
- **Multi-command bodies**: `if true; then echo a; echo b; fi`
- **Mixed with list operators**: `if true; then echo yes; fi && echo ok`
- **Missing `then`**: produces `splash: syntax error: expected 'then'`
- **Missing `fi`**: produces `splash: syntax error: expected 'fi'`

### Testing

- **Unit tests** (10 new in `test_parser.c`): basic if, if-else, if-elif-else, nested if, compound conditions, multi-body, error cases (missing then/fi)
- **Integration tests** (13 new in `test_m8_command_lists.sh`): all branches, nesting, pipes in body, && with if-blocks
