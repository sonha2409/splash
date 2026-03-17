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

### Testing

57 assertions using mock stages:
- Int source: yields 0..N-1, then NULL; verified sequential and lazy pull
- Double filter: transforms each upstream value (x*2)
- Even filter: skips odd values, demonstrating conditional pull
- Chained filters: source → even → double, verified correct composition
- Lazy evaluation: confirmed source only advances when pulled (100-item source, only 2 pulled)
- Drain: verified output for ints (line-per-value), tables (pretty-printed), empty, and NULL
- Free: NULL safety for both free and drain
