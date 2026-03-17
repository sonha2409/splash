#ifndef SPLASH_TABLE_H
#define SPLASH_TABLE_H

#include <stdio.h>
#include <stddef.h>

#include "value.h"

// Column definition: name and expected value type.
typedef struct {
    char *name;         // Owned, heap-allocated
    ValueType type;     // Hint for display (e.g., right-align numbers)
} Column;

// A single row: array of owned Value pointers, one per column.
typedef struct {
    Value **values;     // Array of col_count owned Value*
    size_t count;       // Number of values (== table's col_count)
} Row;

// Columnar structured data table.
// Created via table_new(). Caller takes ownership and must call table_free().
struct Table {
    Column *columns;    // Array of col_count Column definitions
    size_t col_count;

    Row *rows;          // Array of rows
    size_t row_count;
    size_t row_capacity;
};

// Creates a new empty table with the given column schema.
// col_names and col_types are copied — caller retains ownership of the arrays.
// Returns NULL if col_count is 0.
Table *table_new(const char **col_names, const ValueType *col_types,
                 size_t col_count);

// Frees the table, all rows, and all cell values.
void table_free(Table *t);

// Deep copy of the table. Caller takes ownership.
Table *table_clone(const Table *t);

// Appends a row to the table. Takes ownership of each Value* in the values array.
// The values array itself is NOT taken — only its contents.
// count must equal table's col_count; returns false on mismatch.
bool table_add_row(Table *t, Value **values, size_t count);

// Returns a borrowed pointer to the cell value at (row, col).
// Returns NULL if out of bounds.
Value *table_get(const Table *t, size_t row, size_t col);

// Pretty-prints the table with aligned columns and a header separator.
void table_print(const Table *t, FILE *out);

// Returns the number of rows.
size_t table_row_count(const Table *t);

// Returns the number of columns.
size_t table_col_count(const Table *t);

// Finds a column index by name. Returns -1 if not found.
int table_col_index(const Table *t, const char *name);

#endif // SPLASH_TABLE_H
