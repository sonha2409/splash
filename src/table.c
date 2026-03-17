#include "table.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

#define TABLE_INITIAL_ROW_CAP 16


Table *table_new(const char **col_names, const ValueType *col_types,
                 size_t col_count) {
    if (col_count == 0) {
        return NULL;
    }
    Table *t = xmalloc(sizeof(Table));
    t->col_count = col_count;
    t->columns = xmalloc(col_count * sizeof(Column));
    for (size_t i = 0; i < col_count; i++) {
        t->columns[i].name = xstrdup(col_names[i]);
        t->columns[i].type = col_types[i];
    }
    t->rows = xmalloc(TABLE_INITIAL_ROW_CAP * sizeof(Row));
    t->row_count = 0;
    t->row_capacity = TABLE_INITIAL_ROW_CAP;
    return t;
}


void table_free(Table *t) {
    if (!t) {
        return;
    }
    for (size_t i = 0; i < t->row_count; i++) {
        for (size_t j = 0; j < t->rows[i].count; j++) {
            value_free(t->rows[i].values[j]);
        }
        free(t->rows[i].values);
    }
    free(t->rows);
    for (size_t i = 0; i < t->col_count; i++) {
        free(t->columns[i].name);
    }
    free(t->columns);
    free(t);
}


Table *table_clone(const Table *t) {
    if (!t) {
        return NULL;
    }
    const char **names = xmalloc(t->col_count * sizeof(char *));
    ValueType *types = xmalloc(t->col_count * sizeof(ValueType));
    for (size_t i = 0; i < t->col_count; i++) {
        names[i] = t->columns[i].name;
        types[i] = t->columns[i].type;
    }
    Table *clone = table_new(names, types, t->col_count);
    free(names);
    free(types);

    for (size_t i = 0; i < t->row_count; i++) {
        Value **vals = xmalloc(t->col_count * sizeof(Value *));
        for (size_t j = 0; j < t->col_count; j++) {
            vals[j] = value_clone(t->rows[i].values[j]);
        }
        table_add_row(clone, vals, t->col_count);
        free(vals);
    }
    return clone;
}


bool table_add_row(Table *t, Value **values, size_t count) {
    if (!t || count != t->col_count) {
        return false;
    }
    if (t->row_count >= t->row_capacity) {
        t->row_capacity *= 2;
        t->rows = xrealloc(t->rows, t->row_capacity * sizeof(Row));
    }
    Row *row = &t->rows[t->row_count];
    row->values = xmalloc(count * sizeof(Value *));
    row->count = count;
    for (size_t i = 0; i < count; i++) {
        row->values[i] = values[i];
    }
    t->row_count++;
    return true;
}


Value *table_get(const Table *t, size_t row, size_t col) {
    if (!t || row >= t->row_count || col >= t->col_count) {
        return NULL;
    }
    return t->rows[row].values[col];
}


// Returns true if the type should be right-aligned (numeric types).
static bool is_right_aligned(ValueType type) {
    return type == VALUE_INT || type == VALUE_FLOAT;
}


void table_print(const Table *t, FILE *out) {
    if (!t || t->col_count == 0) {
        return;
    }

    // Compute column widths — start with header name lengths
    size_t *widths = xcalloc(t->col_count, sizeof(size_t));
    for (size_t c = 0; c < t->col_count; c++) {
        widths[c] = strlen(t->columns[c].name);
    }

    // Convert all cells to strings and measure widths
    char ***cell_strs = NULL;
    if (t->row_count > 0) {
        cell_strs = xmalloc(t->row_count * sizeof(char **));
        for (size_t r = 0; r < t->row_count; r++) {
            cell_strs[r] = xmalloc(t->col_count * sizeof(char *));
            for (size_t c = 0; c < t->col_count; c++) {
                Value *v = t->rows[r].values[c];
                if (v && v->type != VALUE_NIL) {
                    cell_strs[r][c] = value_to_string(v);
                } else {
                    cell_strs[r][c] = xstrdup("");
                }
                size_t len = strlen(cell_strs[r][c]);
                if (len > widths[c]) {
                    widths[c] = len;
                }
            }
        }
    }

    // Print header row with 1-space padding on each side
    for (size_t c = 0; c < t->col_count; c++) {
        if (is_right_aligned(t->columns[c].type)) {
            fprintf(out, " %*s", (int)widths[c], t->columns[c].name);
        } else {
            fprintf(out, " %-*s", (int)widths[c], t->columns[c].name);
        }
    }
    fputc('\n', out);

    // Print separator using Unicode box-drawing character ─ (U+2500)
    // Each column gets width + 1 leading space worth of ─ characters
    // ─ is 3 bytes in UTF-8: 0xE2 0x94 0x80
    for (size_t c = 0; c < t->col_count; c++) {
        size_t chars = widths[c] + 1; // +1 for leading space
        for (size_t i = 0; i < chars; i++) {
            fputs("\xe2\x94\x80", out);
        }
    }
    fputc('\n', out);

    // Print data rows
    for (size_t r = 0; r < t->row_count; r++) {
        for (size_t c = 0; c < t->col_count; c++) {
            if (is_right_aligned(t->columns[c].type)) {
                fprintf(out, " %*s", (int)widths[c], cell_strs[r][c]);
            } else {
                fprintf(out, " %-*s", (int)widths[c], cell_strs[r][c]);
            }
        }
        fputc('\n', out);
    }

    // Cleanup
    if (cell_strs) {
        for (size_t r = 0; r < t->row_count; r++) {
            for (size_t c = 0; c < t->col_count; c++) {
                free(cell_strs[r][c]);
            }
            free(cell_strs[r]);
        }
        free(cell_strs);
    }
    free(widths);
}


size_t table_row_count(const Table *t) {
    return t ? t->row_count : 0;
}

size_t table_col_count(const Table *t) {
    return t ? t->col_count : 0;
}

int table_col_index(const Table *t, const char *name) {
    if (!t || !name) {
        return -1;
    }
    for (size_t i = 0; i < t->col_count; i++) {
        if (strcmp(t->columns[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}
