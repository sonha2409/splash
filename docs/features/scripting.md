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
