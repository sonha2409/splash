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

## 8.6 Functions

### Design

```
fname() { commands; }
```

Shell functions: define by name, store body text, execute in the current shell context with positional parameters. Functions are checked after builtins but before external commands, and shadow structured builtins if they share a name.

### Implementation

**New module**: `functions.c/h` — a simple dynamic array mapping function names to body source strings.
- `functions_define(name, body_src)` — store or replace a definition
- `functions_lookup(name)` — returns body source or NULL
- `functions_unset(name)` — remove a definition
- `functions_free_all()` — cleanup at exit

**New AST node**: `FunctionDef` in `command.h` with `name` and `body_src` (raw body text). Added `NODE_FUNCTION_DEF` to the `Node` tagged union.

**Parser**: `parse_entry()` checks for function definition syntax (`WORD LPAREN RPAREN`) via `is_function_def()` lookahead before checking compound keywords. `parse_function_def()` consumes `name ( ) {`, parses the body via `parse_command_list_until({"}"})`, extracts raw source text, and expects `}`. `{` and `}` are treated as regular WORD tokens by the tokenizer, detected in context by the parser.

**Executor**: `execute_node()` handles `NODE_FUNCTION_DEF` by calling `functions_define()`. In `executor_execute()`, after the builtin check, `functions_lookup(argv[0])` is checked. If a function is found, positional parameters are pushed, the body is executed via `executor_execute_line()`, and parameters are popped.

**Positional parameters**: `expand.c` now has a stack of parameter frames (`ParamFrame`). `expand_push_params()` pushes a new frame when a function is called; `expand_pop_params()` restores the caller's frame. This supports nested and recursive function calls with proper parameter isolation.

**Tokenizer**: Extended `read_and_expand_var()` to handle `$0`-`$9`, `$#`, `$@`, `$*` as single-character specials (previously only `$?`, `$$`, `$!`, `$_` were handled). Multi-digit positional parameters (`${10}`, `${11}`, ...) work via the `${VAR}` form.

**`expand_variable()`** updated to handle:
- `$0` → `"splash"` (shell name)
- `$1`-`$9` → positional parameters from current frame
- Multi-digit via `${N}` → positional parameters by index
- `$#` → count of parameters
- `$@`, `$*` → space-joined parameters

### Edge Cases

- **Function shadows structured builtin**: `count()` overrides the `count` structured filter — `executor_execute()` skips the structured builtin path when a function with the same name exists
- **Recursive functions**: Work naturally through `executor_execute_line()` re-entrance; each call pushes its own parameter frame
- **Function redefinition**: Overwrites previous definition
- **Nested calls**: `outer()` calling `inner()` — each has isolated parameters via the frame stack
- **No args**: `$#` is 0, `$1`-`$9` expand to empty string, `$@`/`$*` expand to empty
- **Missing `{`**: Produces `splash: syntax error: expected '{' in function 'name'`
- **Keywords not treated as functions**: `if`, `for`, etc. are checked first by `is_compound_keyword()`
- **Word splitting on `$@`**: Currently `$@` expands to a single token during tokenization (full word splitting is not yet implemented)

### Testing

- **Unit tests** (4 new in `test_parser.c`): basic function def, function with list, keyword not parsed as function, missing brace error
- **Integration tests** (16 new in `test_m8_functions.sh`): basic call, multiple commands, `$1`-`$2`, `$#`, `$@`, `$*`, `${1}` braced, redefinition, nested calls, parameter isolation, recursion, if in function, for in function, `$0`

## 8.7 `local` Variables

### Design

```
local VAR=VALUE
local VAR
```

`local` creates function-scoped variables. When the function returns, each `local` variable is restored to its previous value (or unset if it wasn't defined before the function call).

### Implementation

**No new AST node or parser change** — `local` is implemented purely as a builtin command.

**Approach — save/restore on ParamFrame**: The existing `ParamFrame` stack (used for positional parameters) is extended with a `SavedLocal` array. Each `local` call saves the variable's current value onto the frame, then sets the new value via `setenv()`. When `expand_pop_params()` runs on function return, all saved locals are restored in reverse order.

New types in `expand.c`:
- `SavedLocal` — struct with `name` (owned) and `old_value` (owned, NULL if was unset)
- `ParamFrame` extended with `locals`, `num_locals`, `locals_capacity`

New functions in `expand.c/h`:
- `expand_in_function()` — returns 1 if inside a function (param_stack != NULL)
- `expand_save_local(name, value)` — saves current value and sets new one; returns -1 if not in a function

**Builtin**: `builtin_local()` in `builtins.c` parses `VAR=VALUE` or `VAR` form, calls `expand_save_local()`.

### Edge Cases

- **`local` outside function**: Error message, returns 1
- **`local VAR` (no value)**: Saves current value, sets to empty string
- **Double `local` same var**: Only the first save is recorded (avoids losing the original value)
- **Nested functions**: Each frame independently saves/restores; inner function's local doesn't corrupt outer's
- **Tokenizer-time expansion**: `$var` in the same tokenized line as `local var=X` won't see the new value (known limitation of single-line expansion model). Callee functions on subsequent lines will see it.

### Testing

- **Integration tests** (8 new in `test_m8_functions.sh`): restore on return, unset var restored, error outside function, no-value form, multiple locals, nested functions, VAR=VALUE visible to callees, double local same var

## 8.8 `return`

### Design

```
return [N]
```

Return from a function with exit status N (default: last command's exit status). Stops execution of the function body immediately.

### Implementation

**No AST or parser changes** — `return` is a pure builtin + executor flag.

**Flag-based approach**: A `return_pending_flag` in `expand.c` signals early exit:
- `builtin_return()` validates we're in a function, sets exit status, sets `return_pending_flag = 1`
- `executor_execute_list()` checks `expand_return_pending()` after each node and breaks early
- The function call site in `executor_execute()` clears the flag after `expand_pop_params()`

This ensures `return` propagates up through nested command lists (if/for/while bodies) but stops at the function boundary.

### Edge Cases

- **`return` outside function**: Error message, returns 1
- **`return` without arg**: Uses `expand_get_last_status()`
- **`return` in if/for/while body**: Propagates up through all nested command lists
- **`return` with `local`**: `expand_pop_params()` restores locals before flag is cleared
- **Nested functions**: Inner `return` only exits inner function; outer continues normally

### Testing

- **Integration tests** (7 new in `test_m8_functions.sh`): return with code, return without arg, return 0, error outside function, return in if, doesn't stop caller, restores locals

## 8.9 Here-documents

### Design

```
command <<DELIMITER
body text
DELIMITER

command <<'DELIMITER'   # no variable expansion
body text
DELIMITER

command <<-DELIMITER    # strip leading tabs
	body text
DELIMITER
```

Feed literal text as stdin to a command. The delimiter is any word. Lines between `<<DELIMITER` and a line containing only `DELIMITER` form the body.

### Implementation

**Three-layer approach**:

1. **Main loop** (`main.c`): `find_heredoc_delim()` scans a line for `<<`. If found, `collect_heredoc()` reads subsequent lines via `editor_readline()` until the delimiter line, concatenating everything into a single multi-line string. This handles both interactive and non-interactive (pipe/script) input.

2. **Tokenizer** (`tokenizer.c`): New `TOKEN_HEREDOC` token type. When tokenizing `<<`, the tokenizer reads the delimiter, scans forward in the (now multi-line) input to collect the body, emits `TOKEN_HEREDOC` with the body text as value, and sets `heredoc_skip_to` so that when the newline at end-of-first-line is reached, the tokenizer jumps past the body. This preserves any tokens after `<<DELIM` on the same line (e.g., `| grep foo`).

3. **Executor** (`executor.c`): `REDIRECT_HEREDOC` writes the body text to a pipe and `dup2()`s the read end to stdin.

**Variable expansion**: New `expand_heredoc_vars()` in `tokenizer.c` performs `$VAR` and `${VAR}` expansion on the body for unquoted delimiters. Quoted delimiters (`<<'EOF'` or `<<"EOF"`) suppress expansion.

**Tab stripping**: `<<-` strips leading tab characters from each body line. Implemented both in `collect_heredoc()` (delimiter matching ignores leading tabs) and in the tokenizer's body builder.

### Edge Cases

- **Empty body**: `<<EOF` followed immediately by `EOF` — zero-length body, valid
- **Quoted delimiter**: `<<'EOF'` or `<<"EOF"` — no variable expansion
- **Tab stripping**: `<<-EOF` — leading tabs removed from body lines and delimiter line
- **Pipeline**: `cat <<EOF | grep foo` — tokens after `<<DELIM` on same line are preserved
- **Special characters**: `*`, `?`, `|`, etc. in body — passed through literally
- **Missing delimiter**: Warning printed, body extends to end of input

### Testing

- **Integration tests** (9 new in `test_m8_heredoc.sh`): basic, empty, variable expansion, quoted delimiter, pipeline, tab stripping, multiple lines, special chars, double-quoted delimiter

## 8.10 Arithmetic Expansion

### Design

```
$((expr))
```

Evaluates integer arithmetic expressions and substitutes the result as a string. Supports standard operators with correct precedence, parenthesized grouping, variable references, and unary operators.

### Implementation

**New module**: `arith.c/h` — a self-contained recursive descent parser+evaluator for arithmetic expressions.

Grammar:
```
expr     -> add_expr
add_expr -> mul_expr (('+' | '-') mul_expr)*
mul_expr -> unary (('*' | '/' | '%') unary)*
unary    -> ('+' | '-') unary | primary
primary  -> NUMBER | VARIABLE | '$' VARIABLE | '(' expr ')'
```

**Tokenizer** (`tokenizer.c`): Two integration points — the unquoted `$` handler and the double-quoted `$` handler in `read_word()`. Both check for `$((` before `$(` to distinguish arithmetic from command substitution (two-char lookahead).

New helper functions:
- `find_matching_arith()` — finds the closing `))` while tracking inner parenthesis depth. Unlike `find_matching_paren()` (which tracks `()`), this counts single `(` and `)` to correctly handle expressions like `$(( (1+2) * 3 ))` and `$((-(-3)))`.
- `handle_arith_expansion()` — extracts the expression between `$((` and `))`, calls `arith_eval()`, converts the result to a string, and appends it to the word buffer.

**Variable handling**: Bare identifiers (e.g., `x` in `$((x + 1))`) are treated as variable references per POSIX. Both bare names and `$var`/`${var}` forms are supported. Unset or non-numeric variables evaluate to 0.

**Error handling**: Division by zero prints an error to stderr and evaluates to 0. Syntax errors (unexpected characters, unterminated expressions) also print errors and evaluate to 0.

### Edge Cases

- **Precedence**: `2 + 3 * 4` evaluates to 14, not 20
- **Nested parens**: `$(( ((2 + 3)) ))` works correctly
- **Inner parens in closing context**: `$((-(-3)))` — the `find_matching_arith` function tracks inner `()` depth to avoid consuming `)` from a paren group as part of the closing `))`
- **Whitespace**: Freely allowed around operators and parens
- **Division by zero**: Error message, evaluates to 0
- **Unset variables**: Evaluate to 0 (POSIX behavior)
- **Inside double quotes**: `"result=$((3+4))"` expands to `result=7`
- **Multiple in one string**: `"a=$((1+1)) b=$((2+2))"` works

### Testing

- **Integration tests** (21 in `test_m8_arith.sh`): basic ops (+, -, *, /, %), precedence, parentheses, nested parens, unary +/-, double negation, bare variables, $var variables, unset var, inside double quotes, multiple in quotes, whitespace, no whitespace, division by zero, combined with string context
