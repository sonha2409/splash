# Structured Data Pipes

Design and implementation notes for Milestone 7 — splash's structured data pipe system.

## Overview

Splash differentiates itself from traditional shells by supporting structured data alongside text. The text pipe `|` works exactly like any POSIX shell. The structured pipe `|>` passes typed `Value` and `Table` data between pipeline stages, enabling filtering, sorting, and column selection without parsing text.

## Value Type (7.1)

### Design

All structured data is represented by a tagged union `Value` with seven variants:

| Type | C Storage | Description |
|------|-----------|-------------|
| `VALUE_STRING` | `char *` (owned) | Heap-allocated string |
| `VALUE_INT` | `int64_t` | 64-bit signed integer |
| `VALUE_FLOAT` | `double` | IEEE 754 double |
| `VALUE_BOOL` | `bool` | `true` / `false` |
| `VALUE_NIL` | (none) | Absence of a value |
| `VALUE_TABLE` | `Table *` (owned) | Columnar structured data (7.2) |
| `VALUE_LIST` | `ValueList` (inline) | Dynamic array of `Value *` |

### Why a tagged union?

- **Simplicity**: One type covers all cases — no class hierarchies, no vtables.
- **Ownership clarity**: Each `Value` owns its payload. `value_free()` recursively releases everything.
- **C-natural**: Tagged unions are the standard C idiom for variant types. No external dependencies needed.

### Memory model

- **Constructors** (`value_string()`, `value_int()`, etc.) return heap-allocated `Value *`. Caller takes ownership.
- **`value_free()`** recursively frees — strings, list items, and (eventually) tables.
- **`value_clone()`** performs a deep copy — no shared state between original and clone.
- **`value_list_append()`** transfers ownership of the appended item to the list.
- **`value_list_get()`** returns a borrowed pointer — caller must NOT free it.

### ValueList

`ValueList` is an inline dynamic array inside `VALUE_LIST` values:

```c
typedef struct {
    struct Value **items;   // Array of owned Value pointers
    size_t count;
    size_t capacity;
} ValueList;
```

Initial capacity is 8, doubles on growth via `xrealloc()`. Lists can be nested (list of lists) — `value_free()` and `value_clone()` handle recursion.

### API

| Function | Description |
|----------|-------------|
| `value_string(s)` | New string value (NULL → empty string) |
| `value_int(n)` | New integer value |
| `value_float(f)` | New float value |
| `value_bool(b)` | New boolean value |
| `value_nil()` | New nil value |
| `value_list()` | New empty list |
| `value_free(v)` | Recursive destructor |
| `value_clone(v)` | Deep copy |
| `value_to_string(v)` | Human-readable representation (caller frees) |
| `value_type_name(t)` | Static type name string |
| `value_equal(a, b)` | Equality comparison (element-wise for lists) |
| `value_list_append(list, item)` | Append item (ownership transferred) |
| `value_list_get(list, i)` | Get item at index (borrowed pointer) |
| `value_list_count(list)` | Number of items |

### Edge cases handled

- `value_string(NULL)` → treated as empty string `""`
- `value_free(NULL)` → no-op
- `value_clone(NULL)` → returns `NULL`
- `value_to_string(NULL)` → returns `"(null)"`
- List ops on non-list values → return 0 / NULL gracefully
- `value_equal()` with `NULL` arguments → `NULL == NULL` is true, otherwise false
- Table clone/free fully wired into value_clone/value_free

### Testing

78 assertions covering:
- All constructor types and basic values
- Type name mapping for all 7 types
- `value_to_string()` for all types including nested lists
- Equality: same type, different type, NULL, lists with matching/differing elements
- Clone: deep copy verification (different pointers, equal values)
- List: append, get, growth beyond initial capacity, nested lists, boundary checks
- Free: NULL safety

---

## Table Type (7.2)

### Design

`Table` is a columnar data structure: named/typed columns with dynamically growing rows of `Value*` cells.

```c
typedef struct {
    char *name;         // Column name (owned)
    ValueType type;     // Hint for display alignment
} Column;

typedef struct {
    Value **values;     // Array of col_count owned Value*
    size_t count;
} Row;

struct Table {
    Column *columns;
    size_t col_count;
    Row *rows;
    size_t row_count;
    size_t row_capacity;
};
```

### Why this layout?

- **Row-major storage** keeps related values together — good for sequential iteration (the common case in `where`, `select`, `sort` filters).
- **Column metadata** (name + type) is separate from data — schema is defined once, rows just store values.
- **Dynamic row array** starts at capacity 16, doubles on growth — amortized O(1) append.

### Memory model

- `table_new()` copies column names — caller retains ownership of the input arrays.
- `table_add_row()` takes ownership of each `Value*` in the array (but NOT the array itself — it can be stack-allocated).
- `table_get()` returns a borrowed pointer.
- `table_free()` frees all rows, all cell values, column names, and the table itself.
- `table_clone()` deep-copies everything including all cell values.

### Pretty-printing

`table_print()` outputs aligned columns with a Unicode separator:

```
 name   size  type
─────────────────────
 foo.c  1234  file
 bar/    256  dir
```

Algorithm:
1. Measure max width per column (header name vs cell content)
2. Print header with padding (left-align strings, right-align INT/FLOAT)
3. Print `─` (U+2500) separator spanning full width
4. Print data rows with same alignment

NIL cells render as empty strings (not "nil").

### API

| Function | Description |
|----------|-------------|
| `table_new(names, types, n)` | Create table with column schema |
| `table_free(t)` | Free table and all contents |
| `table_clone(t)` | Deep copy |
| `table_add_row(t, values, n)` | Append row (ownership of values transferred) |
| `table_get(t, row, col)` | Borrowed pointer to cell |
| `table_print(t, out)` | Pretty-print to FILE* |
| `table_row_count(t)` | Number of rows |
| `table_col_count(t)` | Number of columns |
| `table_col_index(t, name)` | Find column by name (-1 if not found) |

### Integration with Value

- `value_table(Table *t)` constructor takes ownership of the table.
- `value_free()` calls `table_free()` for `VALUE_TABLE`.
- `value_clone()` calls `table_clone()` for `VALUE_TABLE`.

### Testing

61 assertions covering:
- Construction: basic, zero columns (returns NULL), column name copying
- Add row: basic, column count mismatch rejection, growth beyond initial capacity
- Get: basic cell access, out-of-bounds returns NULL
- Column index: found, not found, NULL inputs
- Clone: deep copy verification, NULL, empty table
- Free: NULL safety
- Print: header/data presence, separator character, right-alignment of numbers, NIL cells as empty
- Value wrapper: constructor, clone integration

---

## Lazy Iterator Protocol (7.4)

### Design

Structured pipelines use **pull-based lazy evaluation**. Each stage is an iterator that only computes the next value when downstream asks for it.

```c
typedef struct PipelineStage {
    Value *(*next)(struct PipelineStage *self);   // Pull next value, NULL = done
    void (*free_fn)(struct PipelineStage *self);  // Free stage-specific state
    void *state;                                   // Opaque per-stage data
    struct PipelineStage *upstream;                // Source stage (NULL for roots)
} PipelineStage;
```

### Why pull-based?

- **Lazy**: If `first 5` only needs 5 rows, upstream never produces more. A structured `ls` on a million-file directory stops after 5 iterations.
- **Simple**: No threading, no buffering, no backpressure. Just function calls on the stack.
- **Composable**: Stages snap together — each only knows about its `next()` contract and its upstream.

### Stage types

- **Source** (upstream = NULL): Generates values from system data. Examples: structured `ls`, `ps`, `env`.
- **Filter** (upstream != NULL): Transforms or filters upstream values. Examples: `where`, `sort`, `select`, `first`.

### Execution flow

```
downstream.next()
  → calls upstream.next()
    → calls upstream.next()
      → source produces value
    ← filter transforms value
  ← returns to caller
```

### API

| Function | Description |
|----------|-------------|
| `pipeline_stage_new(next, free, state, upstream)` | Create a stage |
| `pipeline_stage_drain(stage, out)` | Pull all values, print, then free |
| `pipeline_stage_free(stage)` | Free stage chain recursively |

`pipeline_stage_drain()` handles output formatting:
- `VALUE_TABLE` → `table_print()` (pretty-printed columns)
- All other types → `value_to_string()` + newline

### Memory ownership

- `pipeline_stage_new()` takes ownership of `upstream` and `state`.
- `pipeline_stage_free()` recursively frees the entire chain (upstream first, then current stage).
- `pipeline_stage_drain()` consumes the stage — calls `pipeline_stage_free()` when done.
- Each `Value*` returned by `next()` is owned by the caller.

### `pipeline_stage_drain_to_fd()` (added in 7.5)

Variant of `pipeline_stage_drain()` that writes to a raw file descriptor instead of a `FILE *`. Used by the executor's auto-serialize bridge to pipe structured data into external commands. Opens the fd as a `FILE *` via `fdopen()`, drains, then `fclose()` (which also closes the underlying fd).

### Testing

57 assertions using mock stages (+ 17 from 7.5):
- Int source: yields 0..N-1, then NULL; verified sequential and lazy pull
- Double filter: transforms each upstream value (x*2)
- Even filter: skips odd values, demonstrating conditional pull
- Chained filters: source → even → double, verified correct composition
- Lazy evaluation: confirmed source only advances when pulled (100-item source, only 2 pulled)
- Drain: verified output for ints (line-per-value), tables (pretty-printed), empty, and NULL
- Free: NULL safety for both free and drain
- Drain-to-fd: ints via pipe, table via pipe, empty source, NULL stage, bad fd

---

## Structured `ls` (7.6)

### Design

The first structured builtin. `ls` produces a `Table` with 5 columns:

| Column | Type | Description |
|--------|------|-------------|
| `name` | STRING | File/directory name |
| `size` | INT | Size in bytes |
| `permissions` | STRING | `rwxr-xr-x` format |
| `modified` | STRING | `YYYY-MM-DD HH:MM` format |
| `type` | STRING | `file`, `dir`, `symlink`, `fifo`, `socket`, `block`, `char` |

### Dual behavior

- **Standalone or `|>`**: Uses the structured builtin — produces a table, pretty-printed or piped as structured data.
- **Text pipe `|`**: Falls through to `/bin/ls` via `execvp()`, since `ls` is not registered in `builtin_is_builtin()`.

### Implementation

- `builtin_is_structured("ls")` → returns 1
- `create_ls_stage()` → creates a `PipelineStage` with `LsStageState`
- The stage's `next()` function:
  1. Opens directory with `opendir()` (or stats single file with `lstat()`)
  2. Iterates entries, calling `lstat()` on each
  3. Skips `.` and `..`, includes dotfiles
  4. Builds a Table with all entries, returns it as `VALUE_TABLE`
  5. Returns NULL on subsequent calls (single-yield source)

### Edge cases

- **Single file argument**: `ls file.txt` → 1-row table with that file's info
- **Nonexistent path**: Error message to stderr, returns empty table
- **Unstateable entries**: Silently skipped (e.g., broken symlinks in some cases)
- **Symlinks**: Uses `lstat()` so symlinks show as type `symlink` rather than their target type

### Testing

18 integration test assertions:
- Column headers present (name, size, permissions, modified, type)
- Files and directories listed, dotfiles included
- Type strings correct (file, dir)
- `.` and `..` excluded
- Single file argument works
- Nonexistent path shows error
- `ls |> cat` auto-serializes through pipe

---

## Structured `ps` (7.7)

### Design

Structured `ps` lists all visible processes as a Table using macOS `libproc` APIs.

| Column | Type | Description |
|--------|------|-------------|
| `pid` | INT | Process ID |
| `name` | STRING | Process name from `proc_name()` |
| `cpu_time` | FLOAT | Total CPU time in seconds (user + system) |
| `mem` | INT | Resident memory in bytes |
| `status` | STRING | `idle`, `running`, `sleeping`, `stopped`, `zombie`, `unknown` |

### Implementation

Uses three `libproc` calls:
1. `proc_listallpids(NULL, 0)` — get estimated process count
2. `proc_listallpids(pids, size)` — fill pid array
3. For each pid: `proc_name()`, `proc_pidinfo(PROC_PIDTASKINFO)` for CPU/mem, `proc_pidinfo(PROC_PIDTBSDINFO)` for status

Processes where `proc_name()` fails (permission denied) are silently skipped — this is expected behavior for processes owned by other users.

### Edge cases

- Pid 0 (kernel_task) skipped — `proc_name()` returns empty
- Permission-denied processes: skipped gracefully
- CPU time is total accumulated (user + system) in seconds, not a percentage — avoids the need for sampling
- `ps | grep` falls through to `/bin/ps` via `execvp()`

### Testing

10 integration test assertions:
- All 5 column headers present
- Separator line present
- At least one `running` process listed
- `splash` process visible
- `ps |> cat` auto-serializes correctly

---

## Structured `find` (7.8)

### Design

Recursive directory walk producing a Table with 4 columns:

| Column | Type | Description |
|--------|------|-------------|
| `path` | STRING | Full relative path |
| `name` | STRING | Basename |
| `size` | INT | Size in bytes |
| `type` | STRING | `file`, `dir`, `symlink`, etc. |

### Implementation

- `find_walk()` recursively descends using `opendir()`/`readdir()`/`lstat()`
- Skips `.` and `..`, includes dotfiles
- Only recurses into real directories (not symlinks) to avoid infinite loops
- Unreadable directories are silently skipped
- Single file argument produces a 1-row table

### Testing

18 integration test assertions covering recursive discovery, types, full paths, dotfiles, single file, nonexistent path, and `|>` auto-serialize.

---

## Structured `env` (7.9)

### Design

Simplest structured builtin — iterates the `environ` array and splits each entry on the first `=` to produce `key`/`value` columns.

| Column | Type | Description |
|--------|------|-------------|
| `key` | STRING | Environment variable name |
| `value` | STRING | Environment variable value |

Entries without `=` are skipped (shouldn't happen in practice).

### Testing

8 integration test assertions: column headers, separator, standard vars (HOME, PATH, USER), `|>` auto-serialize.

---

## Auto-serialize (7.5)

### Design

When a structured pipeline segment (connected by `|>`) ends and the next command is an external process (connected by `|` or at the end of the pipeline), structured data must be serialized to text. This is the **auto-serialize bridge**.

### How it works

The executor detects `|>` in the pipeline's `pipe_types[]` array:

1. **Detection**: `pipeline_has_structured()` scans for any `PIPE_STRUCTURED` entries.
2. **Segment identification**: Starting from the first `|>`, find the contiguous run of structured builtins/filters.
3. **Serialization bridge**: If the structured segment feeds into an external command:
   - Create a `pipe()`
   - Fork a serializer child that calls `pipeline_stage_drain_to_fd()` on the write end
   - The external command reads from the read end as normal text stdin
4. **Fallback**: If the first command in a `|>` chain is NOT a structured builtin, all `|>` are treated as plain text pipes. This provides graceful degradation.

### Integration points

- **`builtins.h`**: Added `builtin_is_structured()` and `builtin_create_stage()` — stubs returning 0/NULL until structured builtins are added in 7.6+.
- **`pipeline.h`**: Added `pipeline_stage_drain_to_fd(int fd)` for writing structured output to a pipe fd.
- **`executor.c`**: Added `execute_structured_pipeline()` with the full serialization bridge logic.

### Executor flow for `ls |> where size > 1000 | grep foo`

```
executor_execute()
  → pipeline_has_structured() → true
  → execute_structured_pipeline()
    → builtin_is_structured("ls") → true (after 7.6)
    → builtin_create_stage("ls") → PipelineStage* (source)
    → builtin_create_stage("where", stage) → PipelineStage* (filter)
    → pipe() → [read_fd, write_fd]
    → fork() serializer child:
        pipeline_stage_drain_to_fd(stage, write_fd)
        _exit(0)
    → parent: dup2(read_fd, STDIN_FILENO)
    → execute_pipeline_impl(sub_pipeline with "grep foo")
    → restore STDIN, waitpid serializer
```

### Edge cases

- `|>` between two external commands: falls back to text pipe (no structured source)
- Single structured builtin with no pipe: drains directly to stdout
- Empty structured output: serializer writes nothing, downstream gets EOF
- Serializer child handles SIGPIPE if downstream closes early (default signal behavior after `signals_default()`)

### Testing

5 new `drain_to_fd` tests (17 assertions):
- Integers written to pipe fd, read back and verified
- Table written to pipe fd, headers and data verified
- Empty source produces no output
- NULL stage with valid fd: fd gets closed, no crash
- Bad fd (-1): stage gets freed, no crash

---

## `where` Filter (7.10)

*Documentation pending.*

---

## `sort`, `select`, `first`/`last`, `count` Filters (7.11–7.14)

*Documentation pending.*

---

## `from-csv`, `from-json`, `from-lines` Sources (7.15)

### Design

Three new structured builtins that convert text input into structured tables. Unlike `ls`/`ps`/`env`/`find` which generate data from system APIs, the `from-*` family reads text from stdin and parses it into a `Table`.

### `from-csv`

Reads CSV text from stdin. The first line is treated as column headers.

- **RFC 4180 compliance**: Supports quoted fields — embedded commas, escaped quotes (`""`), and embedded newlines within quoted values are all handled correctly.
- **Type inference**: Each cell is tested in order: int → float → bool → string. The first parse that succeeds determines the cell's `Value` type.
- **Column type hints**: After all rows are parsed, each column's type hint is updated to the dominant type observed across all cells in that column. This ensures `table_print()` applies correct alignment (e.g., right-align numeric columns).

### `from-json`

Reads a JSON array of objects from stdin.

- **Keys → columns**: Object keys become column names. Column order follows the first object's key order; additional keys from later objects are appended.
- **Value mapping**: JSON strings → `VALUE_STRING`, numbers → `VALUE_INT` or `VALUE_FLOAT`, booleans → `VALUE_BOOL`, null → `VALUE_NIL`.
- **Nested structures**: Nested objects and arrays are stringified (serialized back to JSON text) and stored as `VALUE_STRING`.
- **Missing keys**: If an object lacks a key present in other objects, the cell is set to `VALUE_NIL`.
- **Column types**: Inferred from actual values, same dominant-type logic as `from-csv`.

### `from-lines`

Reads text from stdin, splits by newline, produces a single-column table.

| Column | Type | Description |
|--------|------|-------------|
| `line` | STRING | One line of input text |

Empty lines are preserved as empty string values. Trailing newline does not produce an extra empty row.

### Executor integration

The `from-*` builtins work in three contexts:

1. **Standalone with redirect**: `from-csv < file.csv` — the executor opens the redirect, the structured builtin reads from stdin, drains to stdout.
2. **With structured chain**: `from-csv < file.csv |> sort age` — redirect feeds the source, `|>` connects filters. The executor builds the full structured pipeline and drains it.
3. **Text pipe input**: `cat file.csv | from-csv |> sort age` — a text `|` feeds into the `from-*` source (which reads from stdin), then `|>` connects downstream filters. The executor detects the structured segment starting at `from-csv` and builds the pipeline from there.

In all three cases, the `from-*` builtin's `next()` function reads from stdin (fd 0), which has been set up by the executor via redirects or `dup2()` from an upstream text pipe.

### Known limitation

Embedded newlines in CSV quoted fields are correctly parsed into the `Value` string, but `table_print()` does not escape newlines in cell content. This causes the table display to break across lines. The data is correct internally — only the display is affected.

### Edge cases

- Empty input (no data on stdin): produces an empty table (columns from header only for CSV, no columns for JSON/lines)
- CSV with only a header line: produces a table with columns but zero rows
- JSON with empty array `[]`: produces a table with zero columns and zero rows
- JSON with non-array input: error message to stderr
- Malformed JSON: error message to stderr, returns empty table
- `from-lines` with single line (no trailing newline): produces a 1-row table

## Serializers: `to-csv` and `to-json` (7.16)

### Design

The `to-csv` and `to-json` filters are the inverse of `from-csv` and `from-json`. They consume a `VALUE_TABLE` from upstream and produce a `VALUE_STRING` containing the serialized text. This enables format conversion pipelines:

```
cat data.csv | from-csv |> sort age |> to-json        # CSV → filtered JSON
ls |> where type == "file" |> select name size |> to-csv  # table → CSV
```

### Why filters, not sinks?

These are pipeline **filters** (wrap upstream, produce a value), not terminal sinks. This means:

- They chain naturally: `ls |> to-json` works because `to-json` pulls from `ls`'s stage.
- Their output (`VALUE_STRING`) is printed by `pipeline_stage_drain()` like any other value.
- They could theoretically feed into further stages (though that's unusual).

### Implementation

Both follow the same pattern:

1. On first `next()` call, pull one `VALUE_TABLE` from upstream.
2. Serialize the entire table into a heap-allocated string.
3. Wrap it in a `VALUE_STRING` and return it.
4. Return `NULL` on subsequent calls (single-shot).

State structure:
```c
typedef struct {
    Value *result;    // Serialized string, or NULL
    int yielded;      // 0 = not yet yielded, 1 = done
} ToCsvState / ToJsonState;
```

Non-table upstream values are passed through as-is.

### `to-csv`

Produces RFC 4180 compliant CSV:

- **Header row**: Column names, comma-separated.
- **Data rows**: Values converted to strings via `value_to_string()`.
- **Quoting**: Fields containing commas, double quotes, or newlines are wrapped in double quotes. Internal double quotes are escaped as `""`.
- **NIL values**: Empty field (nothing between commas).
- **Numeric values**: Not quoted (e.g., `30` not `"30"`).

### `to-json`

Produces a JSON array of objects:

```json
[
  {"col1": val1, "col2": val2},
  {"col1": val3, "col2": val4}
]
```

- **Column names** → JSON object keys (always double-quoted).
- **Value types** → JSON types:
  - `VALUE_STRING` → quoted string with escape sequences (`\"`, `\\`, `\n`, `\r`, `\t`, `\b`, `\f`, control chars as `\u00XX`)
  - `VALUE_INT` → unquoted integer
  - `VALUE_FLOAT` → unquoted number (via `%g` format)
  - `VALUE_BOOL` → `true` / `false`
  - `VALUE_NIL` → `null`
  - `VALUE_TABLE`, `VALUE_LIST` → stringified and quoted
- **Empty table**: Produces `[\n]` (empty array).
- **Pretty-printed**: 2-space indentation per object, one object per line.

### Terminal control fix

When the executor runs a text prefix like `cat file | from-csv |> ...`, it forks an intermediate child to execute the text prefix. This child inherits the terminal on stdin, causing `execute_pipeline_impl` to detect interactive mode and call `tcsetpgrp()` — which sends SIGTTOU (since the parent shell still owns the terminal), stopping the child and hanging the pipeline.

Fix: the intermediate child redirects stdin to `/dev/null` before calling `execute_pipeline_impl`, preventing interactive terminal control in the sub-process.

### Edge cases

- Empty table (0 rows): CSV produces header only; JSON produces `[]`
- NIL values: CSV → empty field; JSON → `null`
- Strings with special characters: properly escaped in both formats
- Non-table upstream: passed through unchanged
