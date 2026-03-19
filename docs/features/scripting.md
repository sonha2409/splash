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

## 8.3 `for` Loop

### Design

```
for var in word...; do commands; done
```

The word list supports glob expansion (`for f in *.c`). The body is re-evaluated on each iteration so `$var` expansions reflect the current loop variable.

### Implementation

**New AST node**: `ForCommand` with var name, word list, and **raw body source text** (`body_src`). Added `NODE_FOR` to the `Node` tagged union.

**Key design decision — raw body text**: The tokenizer expands `$var` at tokenization time. For loops that set a variable and re-use it in the body, this means the body must be re-tokenized each iteration. `ForCommand` stores the raw source text of the body (extracted via token position offsets from the original input). The executor calls `executor_execute_line(body_src)` on each iteration, which re-tokenizes and re-expands variables.

This required adding the original input string as a parameter to `parser_parse(tokens, input)`.

**Parser**: Recognizes `for` keyword → expects var name → `in` → collects WORD tokens until `;`/newline/`do` → expects `do` → parses body (via `parse_command_list_until`) to validate and advance past it, then extracts raw source via token positions → expects `done`.

**Executor**: `execute_for_command()` iterates the word list, expanding globs on each word via `expand_glob()`, sets the variable via `setenv()`, and calls `executor_execute_line(body_src)`.

### Edge Cases

- **Glob expansion**: `for f in *.c; do echo $f; done` — each word is glob-expanded before iteration
- **Empty word list**: `for x in ; do echo $x; done` — zero iterations
- **Nested**: `if true; then for x in a b; do echo $x; done; fi` and vice versa
- **Multiple body commands**: `for x in a b; do echo $x; echo ok; done`

### Testing

- **Unit tests** (8 new in `test_parser.c`): basic, multi-body, with list ops, empty words, nested in if, error cases (missing in/do/done)
- **Integration tests** (7 new in `test_m8_command_lists.sh`): variable expansion, globs, pipes in body, nesting

## 8.4 `while` / `until` Loops

### Design

```
while condition; do commands; done
until condition; do commands; done
```

- `while`: loop as long as condition exits with status 0
- `until`: loop as long as condition exits with non-zero status (inverted `while`)

Both condition and body are full command lists. Like `for`, the body and condition are re-evaluated each iteration.

### Implementation

**New AST node**: `WhileCommand` with raw condition source (`cond_src`), raw body source (`body_src`), and `is_until` flag. Single `NODE_WHILE` variant handles both `while` and `until`. Added to the `Node` tagged union.

**Design decision — unified struct**: `while` and `until` differ only in the condition check direction, so they share one struct and parser function (`parse_while_command(p, is_until)`), avoiding code duplication.

**Design decision — raw source text (same as `for`)**: Both `cond_src` and `body_src` store raw substrings from the original input, extracted via token position offsets. The executor re-tokenizes and re-evaluates these each iteration, ensuring variable expansions reflect current state.

**Parser**: `parse_while_command()` consumes the keyword → parses condition via `parse_command_list_until({"do"})` and extracts raw source → expects `do` → parses body via `parse_command_list_until({"done"})` and extracts raw source → expects `done`. `is_compound_keyword()` updated to recognize `"while"` and `"until"`.

**Executor**: `execute_while_command()` loops: evaluates `cond_src` via `executor_execute_line()`, checks exit status (invert for `until`), executes `body_src` if continuing. Returns last body exit status or 0 if body never ran.

### Edge Cases

- **`while false`**: body never runs, returns 0
- **`until true`**: body never runs, returns 0
- **Nested**: `while` inside `if`, `for` inside `while`, etc. all work through raw-body re-evaluation
- **Empty body**: valid, just re-evaluates condition each pass
- **Multi-command condition**: `while test $x -gt 0; do ...` — condition is a full command list
- **No `break`/`continue` yet**: will be added as a separate feature; currently loops must terminate via condition change

### Testing

- **Unit tests** (8 new in `test_parser.c`): basic while, basic until, multi-body, with list ops, nested in if, error cases (missing do/done, empty condition)
- **Manual tests** (9 cases): basic while, while false, until true, until body runs, nesting with if/for, multi-command body, error cases

## 8.5 `case/esac`

### Design

```
case word in
  pattern1) commands ;;
  pattern2|pattern3) commands ;;
  *) commands ;;
esac
```

Pattern matching using `fnmatch()` — supports `*`, `?`, and `[...]` glob patterns. Multiple patterns per clause separated by `|`. The `*` catch-all pattern works naturally.

### Implementation

**Tokenizer**: Added `TOKEN_DSEMI` (`;;`) token type. When the tokenizer sees `;`, it peeks at the next character — if also `;`, it emits a single `TOKEN_DSEMI` token instead of two `TOKEN_SEMICOLON` tokens.

**New AST node**: `CaseCommand` with match word, array of `CaseClause` (each has patterns + raw body source). Added `NODE_CASE` to the `Node` tagged union.

**Parser**: `parse_case_command()` consumes `case` → expects WORD → expects `in` → loops parsing clauses:
- Collect patterns (WORD tokens separated by `TOKEN_PIPE`) until `TOKEN_RPAREN`
- Parse body via `parse_command_list_until({"esac"})`, also stopping at `TOKEN_DSEMI`
- Extract raw body source text
- Consume `;;` or allow final clause without `;;` before `esac`

`parse_command_list_until()` was updated to also stop at `TOKEN_DSEMI`, since `;;` terminates case clause bodies.

**Executor**: `execute_case_command()` iterates clauses, matching each pattern against the word using `fnmatch(pattern, word, 0)`. The tokenizer replaces unquoted `*`/`?` with sentinel bytes (`\x01`/`\x02`) for glob expansion, so the executor unescapes these back to real characters before passing to `fnmatch()`.

### Edge Cases

- **Glob patterns**: `*.txt`, `?b`, `[a-z]*` all work via `fnmatch()`
- **Multiple patterns**: `a|b|c)` — any match triggers the clause
- **Catch-all**: `*)` matches anything (standard POSIX idiom)
- **Last clause without `;;`**: Valid if followed by `; esac` or newline + `esac`
- **No match**: Returns 0, no clause body executed
- **Variable in word**: Expanded at tokenization time; works when variable is set before the line containing `case`
- **Optional leading `(`**: `(pattern)` syntax is accepted (POSIX allows it)

### Testing

- **Unit tests** (9 new in `test_parser.c`): basic, multiple clauses, multi-pattern, with list ops, last without `;;`, nested in if, error cases (missing in/esac/rparen)
- **Tokenizer tests** (1 new, 1 updated in `test_tokenizer.c`): `;;` tokenization, updated adjacent-operators test
- **Manual tests** (7 cases): exact match, wildcard `*`, multi-pattern `|`, glob `*.txt`/`?`, no match, variable, last clause without `;;`
