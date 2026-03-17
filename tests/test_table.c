#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "table.h"
#include "test.h"
#include "value.h"


// Helper: create a simple 3-column table (name:string, size:int, type:string)
static Table *make_test_table(void) {
    const char *names[] = {"name", "size", "type"};
    ValueType types[] = {VALUE_STRING, VALUE_INT, VALUE_STRING};
    Table *t = table_new(names, types, 3);

    Value *row1[] = {value_string("foo.c"), value_int(1234), value_string("file")};
    table_add_row(t, row1, 3);

    Value *row2[] = {value_string("bar/"), value_int(256), value_string("dir")};
    table_add_row(t, row2, 3);

    return t;
}


// --- Construction ---

static void test_table_new_basic(void) {
    const char *names[] = {"a", "b"};
    ValueType types[] = {VALUE_STRING, VALUE_INT};
    Table *t = table_new(names, types, 2);
    ASSERT_NOT_NULL(t);
    ASSERT(table_col_count(t) == 2);
    ASSERT(table_row_count(t) == 0);
    table_free(t);
}

static void test_table_new_zero_cols(void) {
    Table *t = table_new(NULL, NULL, 0);
    ASSERT_NULL(t);
}

static void test_table_col_names_copied(void) {
    char name_buf[16];
    snprintf(name_buf, sizeof(name_buf), "col1");
    const char *names[] = {name_buf};
    ValueType types[] = {VALUE_STRING};
    Table *t = table_new(names, types, 1);
    // Modify original — table should be unaffected
    snprintf(name_buf, sizeof(name_buf), "XXXX");
    ASSERT_STR_EQ(t->columns[0].name, "col1");
    table_free(t);
}


// --- Add row ---

static void test_table_add_row_basic(void) {
    Table *t = make_test_table();
    ASSERT(table_row_count(t) == 2);
    table_free(t);
}

static void test_table_add_row_mismatch(void) {
    const char *names[] = {"x"};
    ValueType types[] = {VALUE_INT};
    Table *t = table_new(names, types, 1);

    // Wrong count — should return false
    Value *vals[] = {value_int(1), value_int(2)};
    bool ok = table_add_row(t, vals, 2);
    ASSERT(!ok);
    ASSERT(table_row_count(t) == 0);

    // Clean up the values we still own (table rejected them)
    value_free(vals[0]);
    value_free(vals[1]);
    table_free(t);
}

static void test_table_add_row_grow(void) {
    const char *names[] = {"n"};
    ValueType types[] = {VALUE_INT};
    Table *t = table_new(names, types, 1);

    // Add more rows than initial capacity (16)
    for (int i = 0; i < 30; i++) {
        Value *v = value_int(i);
        table_add_row(t, &v, 1);
    }
    ASSERT(table_row_count(t) == 30);

    Value *last = table_get(t, 29, 0);
    ASSERT_NOT_NULL(last);
    ASSERT(last->integer == 29);

    table_free(t);
}


// --- Get ---

static void test_table_get_basic(void) {
    Table *t = make_test_table();

    Value *v = table_get(t, 0, 0);
    ASSERT_NOT_NULL(v);
    ASSERT_STR_EQ(v->string, "foo.c");

    v = table_get(t, 0, 1);
    ASSERT(v->integer == 1234);

    v = table_get(t, 1, 2);
    ASSERT_STR_EQ(v->string, "dir");

    table_free(t);
}

static void test_table_get_out_of_bounds(void) {
    Table *t = make_test_table();
    ASSERT_NULL(table_get(t, 5, 0));
    ASSERT_NULL(table_get(t, 0, 5));
    ASSERT_NULL(table_get(NULL, 0, 0));
    table_free(t);
}


// --- Col index ---

static void test_table_col_index_found(void) {
    Table *t = make_test_table();
    ASSERT_INT_EQ(table_col_index(t, "name"), 0);
    ASSERT_INT_EQ(table_col_index(t, "size"), 1);
    ASSERT_INT_EQ(table_col_index(t, "type"), 2);
    table_free(t);
}

static void test_table_col_index_not_found(void) {
    Table *t = make_test_table();
    ASSERT_INT_EQ(table_col_index(t, "nonexistent"), -1);
    ASSERT_INT_EQ(table_col_index(t, NULL), -1);
    ASSERT_INT_EQ(table_col_index(NULL, "x"), -1);
    table_free(t);
}


// --- Clone ---

static void test_table_clone_basic(void) {
    Table *orig = make_test_table();
    Table *clone = table_clone(orig);

    ASSERT_NOT_NULL(clone);
    ASSERT(table_row_count(clone) == table_row_count(orig));
    ASSERT(table_col_count(clone) == table_col_count(orig));

    // Check column names copied
    ASSERT_STR_EQ(clone->columns[0].name, "name");
    ASSERT_STR_EQ(clone->columns[1].name, "size");

    // Check cell values equal but different pointers
    Value *orig_v = table_get(orig, 0, 0);
    Value *clone_v = table_get(clone, 0, 0);
    ASSERT(value_equal(orig_v, clone_v));
    ASSERT(orig_v != clone_v);

    table_free(orig);
    table_free(clone);
}

static void test_table_clone_null(void) {
    ASSERT_NULL(table_clone(NULL));
}

static void test_table_clone_empty(void) {
    const char *names[] = {"a"};
    ValueType types[] = {VALUE_STRING};
    Table *orig = table_new(names, types, 1);
    Table *clone = table_clone(orig);
    ASSERT_NOT_NULL(clone);
    ASSERT(table_row_count(clone) == 0);
    ASSERT(table_col_count(clone) == 1);
    table_free(orig);
    table_free(clone);
}


// --- Free ---

static void test_table_free_null(void) {
    table_free(NULL); // Should not crash
}


// --- Print ---

static void test_table_print_basic(void) {
    Table *t = make_test_table();

    // Capture output to a temp file
    FILE *f = tmpfile();
    ASSERT_NOT_NULL(f);
    table_print(t, f);

    // Read back
    rewind(f);
    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    // Check header names are present
    ASSERT(strstr(buf, "name") != NULL);
    ASSERT(strstr(buf, "size") != NULL);
    ASSERT(strstr(buf, "type") != NULL);

    // Check data is present
    ASSERT(strstr(buf, "foo.c") != NULL);
    ASSERT(strstr(buf, "1234") != NULL);
    ASSERT(strstr(buf, "bar/") != NULL);
    ASSERT(strstr(buf, "dir") != NULL);

    // Check separator is present (─ = \xe2\x94\x80)
    ASSERT(strstr(buf, "\xe2\x94\x80") != NULL);

    table_free(t);
}

static void test_table_print_empty(void) {
    const char *names[] = {"x", "y"};
    ValueType types[] = {VALUE_STRING, VALUE_INT};
    Table *t = table_new(names, types, 2);

    FILE *f = tmpfile();
    ASSERT_NOT_NULL(f);
    table_print(t, f);

    rewind(f);
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    // Should have header and separator but no data rows
    ASSERT(strstr(buf, "x") != NULL);
    ASSERT(strstr(buf, "y") != NULL);
    ASSERT(strstr(buf, "\xe2\x94\x80") != NULL);

    // Count newlines — should be exactly 2 (header + separator)
    int newlines = 0;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == '\n') newlines++;
    }
    ASSERT_INT_EQ(newlines, 2);

    table_free(t);
}

static void test_table_print_right_align_numbers(void) {
    const char *names[] = {"val"};
    ValueType types[] = {VALUE_INT};
    Table *t = table_new(names, types, 1);

    Value *r1[] = {value_int(1)};
    table_add_row(t, r1, 1);

    Value *r2[] = {value_int(1000)};
    table_add_row(t, r2, 1);

    FILE *f = tmpfile();
    table_print(t, f);
    rewind(f);

    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    // "1" should be right-aligned (preceded by spaces)
    // The line with "1" (not "1000") should have leading spaces
    ASSERT(strstr(buf, "    1\n") != NULL);
    ASSERT(strstr(buf, " 1000\n") != NULL);

    table_free(t);
}

static void test_table_print_nil_cells(void) {
    const char *names[] = {"a"};
    ValueType types[] = {VALUE_STRING};
    Table *t = table_new(names, types, 1);

    Value *r[] = {value_nil()};
    table_add_row(t, r, 1);

    FILE *f = tmpfile();
    table_print(t, f);
    rewind(f);

    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    // NIL should render as empty (not "nil")
    ASSERT(strstr(buf, "nil") == NULL);

    table_free(t);
}


// --- Value wrapper ---

static void test_value_table_constructor(void) {
    Table *t = make_test_table();
    Value *v = value_table(t);
    ASSERT_NOT_NULL(v);
    ASSERT_INT_EQ(v->type, VALUE_TABLE);
    ASSERT(v->table == t);
    value_free(v); // Frees the table too
}

static void test_value_table_clone(void) {
    Table *t = make_test_table();
    Value *orig = value_table(t);
    Value *clone = value_clone(orig);
    ASSERT_NOT_NULL(clone);
    ASSERT_INT_EQ(clone->type, VALUE_TABLE);
    ASSERT(clone->table != orig->table); // Deep copy
    ASSERT(table_row_count(clone->table) == 2);
    value_free(orig);
    value_free(clone);
}


// --- Accessors with NULL ---

static void test_table_row_count_null(void) {
    ASSERT(table_row_count(NULL) == 0);
}

static void test_table_col_count_null(void) {
    ASSERT(table_col_count(NULL) == 0);
}


int main(void) {
    printf("test_table:\n");

    // Construction
    test_table_new_basic();
    test_table_new_zero_cols();
    test_table_col_names_copied();

    // Add row
    test_table_add_row_basic();
    test_table_add_row_mismatch();
    test_table_add_row_grow();

    // Get
    test_table_get_basic();
    test_table_get_out_of_bounds();

    // Col index
    test_table_col_index_found();
    test_table_col_index_not_found();

    // Clone
    test_table_clone_basic();
    test_table_clone_null();
    test_table_clone_empty();

    // Free
    test_table_free_null();

    // Print
    test_table_print_basic();
    test_table_print_empty();
    test_table_print_right_align_numbers();
    test_table_print_nil_cells();

    // Value wrapper
    test_value_table_constructor();
    test_value_table_clone();

    // NULL accessors
    test_table_row_count_null();
    test_table_col_count_null();

    TEST_REPORT();
}
