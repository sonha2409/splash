#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "value.h"
#include "test.h"


// --- Constructor / type tests ---

static void test_value_string_basic(void) {
    Value *v = value_string("hello");
    ASSERT_NOT_NULL(v);
    ASSERT_INT_EQ(v->type, VALUE_STRING);
    ASSERT_STR_EQ(v->string, "hello");
    value_free(v);
}

static void test_value_string_empty(void) {
    Value *v = value_string("");
    ASSERT_STR_EQ(v->string, "");
    value_free(v);
}

static void test_value_string_null_input(void) {
    Value *v = value_string(NULL);
    ASSERT_NOT_NULL(v);
    ASSERT_STR_EQ(v->string, "");
    value_free(v);
}

static void test_value_int_basic(void) {
    Value *v = value_int(42);
    ASSERT_INT_EQ(v->type, VALUE_INT);
    ASSERT(v->integer == 42);
    value_free(v);
}

static void test_value_int_negative(void) {
    Value *v = value_int(-100);
    ASSERT(v->integer == -100);
    value_free(v);
}

static void test_value_int_zero(void) {
    Value *v = value_int(0);
    ASSERT(v->integer == 0);
    value_free(v);
}

static void test_value_float_basic(void) {
    Value *v = value_float(3.14);
    ASSERT_INT_EQ(v->type, VALUE_FLOAT);
    ASSERT(v->floating == 3.14);
    value_free(v);
}

static void test_value_float_zero(void) {
    Value *v = value_float(0.0);
    ASSERT(v->floating == 0.0);
    value_free(v);
}

static void test_value_bool_true(void) {
    Value *v = value_bool(true);
    ASSERT_INT_EQ(v->type, VALUE_BOOL);
    ASSERT(v->boolean == true);
    value_free(v);
}

static void test_value_bool_false(void) {
    Value *v = value_bool(false);
    ASSERT(v->boolean == false);
    value_free(v);
}

static void test_value_nil(void) {
    Value *v = value_nil();
    ASSERT_NOT_NULL(v);
    ASSERT_INT_EQ(v->type, VALUE_NIL);
    value_free(v);
}

static void test_value_list_empty(void) {
    Value *v = value_list();
    ASSERT_NOT_NULL(v);
    ASSERT_INT_EQ(v->type, VALUE_LIST);
    ASSERT(value_list_count(v) == 0);
    value_free(v);
}


// --- Type name ---

static void test_value_type_name(void) {
    ASSERT_STR_EQ(value_type_name(VALUE_STRING), "string");
    ASSERT_STR_EQ(value_type_name(VALUE_INT), "int");
    ASSERT_STR_EQ(value_type_name(VALUE_FLOAT), "float");
    ASSERT_STR_EQ(value_type_name(VALUE_BOOL), "bool");
    ASSERT_STR_EQ(value_type_name(VALUE_NIL), "nil");
    ASSERT_STR_EQ(value_type_name(VALUE_TABLE), "table");
    ASSERT_STR_EQ(value_type_name(VALUE_LIST), "list");
}


// --- to_string ---

static void test_value_to_string_string(void) {
    Value *v = value_string("world");
    char *s = value_to_string(v);
    ASSERT_STR_EQ(s, "world");
    free(s);
    value_free(v);
}

static void test_value_to_string_int(void) {
    Value *v = value_int(999);
    char *s = value_to_string(v);
    ASSERT_STR_EQ(s, "999");
    free(s);
    value_free(v);
}

static void test_value_to_string_negative_int(void) {
    Value *v = value_int(-42);
    char *s = value_to_string(v);
    ASSERT_STR_EQ(s, "-42");
    free(s);
    value_free(v);
}

static void test_value_to_string_float(void) {
    Value *v = value_float(2.5);
    char *s = value_to_string(v);
    ASSERT_STR_EQ(s, "2.5");
    free(s);
    value_free(v);
}

static void test_value_to_string_bool(void) {
    Value *t = value_bool(true);
    Value *f = value_bool(false);
    char *st = value_to_string(t);
    char *sf = value_to_string(f);
    ASSERT_STR_EQ(st, "true");
    ASSERT_STR_EQ(sf, "false");
    free(st);
    free(sf);
    value_free(t);
    value_free(f);
}

static void test_value_to_string_nil(void) {
    Value *v = value_nil();
    char *s = value_to_string(v);
    ASSERT_STR_EQ(s, "nil");
    free(s);
    value_free(v);
}

static void test_value_to_string_empty_list(void) {
    Value *v = value_list();
    char *s = value_to_string(v);
    ASSERT_STR_EQ(s, "[]");
    free(s);
    value_free(v);
}

static void test_value_to_string_list(void) {
    Value *v = value_list();
    value_list_append(v, value_int(1));
    value_list_append(v, value_string("two"));
    value_list_append(v, value_bool(true));
    char *s = value_to_string(v);
    ASSERT_STR_EQ(s, "[1, two, true]");
    free(s);
    value_free(v);
}

static void test_value_to_string_null(void) {
    char *s = value_to_string(NULL);
    ASSERT_STR_EQ(s, "(null)");
    free(s);
}


// --- Equality ---

static void test_value_equal_strings(void) {
    Value *a = value_string("abc");
    Value *b = value_string("abc");
    Value *c = value_string("xyz");
    ASSERT(value_equal(a, b));
    ASSERT(!value_equal(a, c));
    value_free(a);
    value_free(b);
    value_free(c);
}

static void test_value_equal_ints(void) {
    Value *a = value_int(10);
    Value *b = value_int(10);
    Value *c = value_int(20);
    ASSERT(value_equal(a, b));
    ASSERT(!value_equal(a, c));
    value_free(a);
    value_free(b);
    value_free(c);
}

static void test_value_equal_floats(void) {
    Value *a = value_float(1.5);
    Value *b = value_float(1.5);
    Value *c = value_float(2.5);
    ASSERT(value_equal(a, b));
    ASSERT(!value_equal(a, c));
    value_free(a);
    value_free(b);
    value_free(c);
}

static void test_value_equal_bools(void) {
    Value *a = value_bool(true);
    Value *b = value_bool(true);
    Value *c = value_bool(false);
    ASSERT(value_equal(a, b));
    ASSERT(!value_equal(a, c));
    value_free(a);
    value_free(b);
    value_free(c);
}

static void test_value_equal_nils(void) {
    Value *a = value_nil();
    Value *b = value_nil();
    ASSERT(value_equal(a, b));
    value_free(a);
    value_free(b);
}

static void test_value_equal_different_types(void) {
    Value *a = value_int(1);
    Value *b = value_string("1");
    ASSERT(!value_equal(a, b));
    value_free(a);
    value_free(b);
}

static void test_value_equal_nulls(void) {
    Value *a = value_int(1);
    ASSERT(!value_equal(a, NULL));
    ASSERT(!value_equal(NULL, a));
    ASSERT(value_equal(NULL, NULL));
    value_free(a);
}

static void test_value_equal_lists(void) {
    Value *a = value_list();
    value_list_append(a, value_int(1));
    value_list_append(a, value_string("x"));

    Value *b = value_list();
    value_list_append(b, value_int(1));
    value_list_append(b, value_string("x"));

    Value *c = value_list();
    value_list_append(c, value_int(1));
    value_list_append(c, value_string("y"));

    ASSERT(value_equal(a, b));
    ASSERT(!value_equal(a, c));

    value_free(a);
    value_free(b);
    value_free(c);
}

static void test_value_equal_lists_different_length(void) {
    Value *a = value_list();
    value_list_append(a, value_int(1));

    Value *b = value_list();
    value_list_append(b, value_int(1));
    value_list_append(b, value_int(2));

    ASSERT(!value_equal(a, b));

    value_free(a);
    value_free(b);
}


// --- Clone ---

static void test_value_clone_string(void) {
    Value *orig = value_string("test");
    Value *clone = value_clone(orig);
    ASSERT(value_equal(orig, clone));
    // Ensure deep copy — different pointers
    ASSERT(orig->string != clone->string);
    value_free(orig);
    value_free(clone);
}

static void test_value_clone_int(void) {
    Value *orig = value_int(77);
    Value *clone = value_clone(orig);
    ASSERT(value_equal(orig, clone));
    value_free(orig);
    value_free(clone);
}

static void test_value_clone_list(void) {
    Value *orig = value_list();
    value_list_append(orig, value_int(1));
    value_list_append(orig, value_string("two"));

    Value *clone = value_clone(orig);
    ASSERT(value_equal(orig, clone));
    // Ensure deep copy — items are different pointers
    ASSERT(orig->list.items[0] != clone->list.items[0]);
    ASSERT(orig->list.items[1] != clone->list.items[1]);

    value_free(orig);
    value_free(clone);
}

static void test_value_clone_nil(void) {
    Value *orig = value_nil();
    Value *clone = value_clone(orig);
    ASSERT(value_equal(orig, clone));
    value_free(orig);
    value_free(clone);
}

static void test_value_clone_null(void) {
    ASSERT(value_clone(NULL) == NULL);
}


// --- List operations ---

static void test_value_list_append_and_get(void) {
    Value *list = value_list();
    value_list_append(list, value_int(10));
    value_list_append(list, value_string("hello"));
    value_list_append(list, value_bool(false));

    ASSERT(value_list_count(list) == 3);

    Value *v0 = value_list_get(list, 0);
    ASSERT_NOT_NULL(v0);
    ASSERT(v0->integer == 10);

    Value *v1 = value_list_get(list, 1);
    ASSERT_NOT_NULL(v1);
    ASSERT_STR_EQ(v1->string, "hello");

    Value *v2 = value_list_get(list, 2);
    ASSERT_NOT_NULL(v2);
    ASSERT(v2->boolean == false);

    ASSERT(value_list_get(list, 3) == NULL);

    value_free(list);
}

static void test_value_list_grow(void) {
    // Verify the list grows beyond initial capacity (8)
    Value *list = value_list();
    for (int i = 0; i < 20; i++) {
        value_list_append(list, value_int(i));
    }
    ASSERT(value_list_count(list) == 20);
    Value *last = value_list_get(list, 19);
    ASSERT_NOT_NULL(last);
    ASSERT(last->integer == 19);
    value_free(list);
}

static void test_value_list_count_non_list(void) {
    Value *v = value_int(5);
    ASSERT(value_list_count(v) == 0);
    value_free(v);
}

static void test_value_list_get_non_list(void) {
    Value *v = value_int(5);
    ASSERT(value_list_get(v, 0) == NULL);
    value_free(v);
}

static void test_value_list_nested(void) {
    Value *inner = value_list();
    value_list_append(inner, value_int(1));
    value_list_append(inner, value_int(2));

    Value *outer = value_list();
    value_list_append(outer, inner);
    value_list_append(outer, value_string("after"));

    ASSERT(value_list_count(outer) == 2);
    Value *got = value_list_get(outer, 0);
    ASSERT_INT_EQ(got->type, VALUE_LIST);
    ASSERT(value_list_count(got) == 2);

    value_free(outer); // Frees inner too
}


// --- Free edge cases ---

static void test_value_free_null(void) {
    // Should not crash
    value_free(NULL);
}


int main(void) {
    printf("test_value:\n");

    // Constructors
    test_value_string_basic();
    test_value_string_empty();
    test_value_string_null_input();
    test_value_int_basic();
    test_value_int_negative();
    test_value_int_zero();
    test_value_float_basic();
    test_value_float_zero();
    test_value_bool_true();
    test_value_bool_false();
    test_value_nil();
    test_value_list_empty();

    // Type name
    test_value_type_name();

    // to_string
    test_value_to_string_string();
    test_value_to_string_int();
    test_value_to_string_negative_int();
    test_value_to_string_float();
    test_value_to_string_bool();
    test_value_to_string_nil();
    test_value_to_string_empty_list();
    test_value_to_string_list();
    test_value_to_string_null();

    // Equality
    test_value_equal_strings();
    test_value_equal_ints();
    test_value_equal_floats();
    test_value_equal_bools();
    test_value_equal_nils();
    test_value_equal_different_types();
    test_value_equal_nulls();
    test_value_equal_lists();
    test_value_equal_lists_different_length();

    // Clone
    test_value_clone_string();
    test_value_clone_int();
    test_value_clone_list();
    test_value_clone_nil();
    test_value_clone_null();

    // List operations
    test_value_list_append_and_get();
    test_value_list_grow();
    test_value_list_count_non_list();
    test_value_list_get_non_list();
    test_value_list_nested();

    // Free
    test_value_free_null();

    TEST_REPORT();
}
