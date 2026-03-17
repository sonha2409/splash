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
- Table clone → returns NIL placeholder (full implementation in 7.2)

### Testing

78 assertions covering:
- All constructor types and basic values
- Type name mapping for all 7 types
- `value_to_string()` for all types including nested lists
- Equality: same type, different type, NULL, lists with matching/differing elements
- Clone: deep copy verification (different pointers, equal values)
- List: append, get, growth beyond initial capacity, nested lists, boundary checks
- Free: NULL safety
