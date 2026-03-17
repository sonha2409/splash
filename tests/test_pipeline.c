#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pipeline.h"
#include "table.h"
#include "test.h"
#include "value.h"
#include "util.h"


// --- Mock source: yields integers 0..N-1, then NULL ---

typedef struct {
    int current;
    int limit;
} IntSourceState;

static Value *int_source_next(PipelineStage *self) {
    IntSourceState *s = self->state;
    if (s->current >= s->limit) {
        return NULL;
    }
    return value_int(s->current++);
}

static void int_source_free(PipelineStage *self) {
    free(self->state);
}

static PipelineStage *make_int_source(int count) {
    IntSourceState *s = xmalloc(sizeof(IntSourceState));
    s->current = 0;
    s->limit = count;
    return pipeline_stage_new(int_source_next, int_source_free, s, NULL);
}


// --- Mock filter: doubles each upstream int value ---

static Value *double_filter_next(PipelineStage *self) {
    Value *v = self->upstream->next(self->upstream);
    if (!v) {
        return NULL;
    }
    int64_t doubled = v->integer * 2;
    value_free(v);
    return value_int(doubled);
}

static PipelineStage *make_double_filter(PipelineStage *upstream) {
    return pipeline_stage_new(double_filter_next, NULL, NULL, upstream);
}


// --- Mock filter: skips odd values (keeps evens) ---

static Value *even_filter_next(PipelineStage *self) {
    for (;;) {
        Value *v = self->upstream->next(self->upstream);
        if (!v) {
            return NULL;
        }
        if (v->integer % 2 == 0) {
            return v;
        }
        value_free(v);
    }
}

static PipelineStage *make_even_filter(PipelineStage *upstream) {
    return pipeline_stage_new(even_filter_next, NULL, NULL, upstream);
}


// --- Mock source: yields a single table value, then NULL ---

typedef struct {
    int yielded;
} TableSourceState;

static Value *table_source_next(PipelineStage *self) {
    TableSourceState *s = self->state;
    if (s->yielded) {
        return NULL;
    }
    s->yielded = 1;

    const char *names[] = {"name", "val"};
    ValueType types[] = {VALUE_STRING, VALUE_INT};
    Table *t = table_new(names, types, 2);

    Value *row1[] = {value_string("alpha"), value_int(10)};
    table_add_row(t, row1, 2);

    Value *row2[] = {value_string("beta"), value_int(20)};
    table_add_row(t, row2, 2);

    return value_table(t);
}

static void table_source_free(PipelineStage *self) {
    free(self->state);
}

static PipelineStage *make_table_source(void) {
    TableSourceState *s = xmalloc(sizeof(TableSourceState));
    s->yielded = 0;
    return pipeline_stage_new(table_source_next, table_source_free, s, NULL);
}


// --- Tests ---

static void test_pipeline_source_basic(void) {
    PipelineStage *src = make_int_source(5);

    for (int i = 0; i < 5; i++) {
        Value *v = src->next(src);
        ASSERT_NOT_NULL(v);
        ASSERT(v->integer == i);
        value_free(v);
    }

    // Exhausted
    Value *v = src->next(src);
    ASSERT_NULL(v);

    pipeline_stage_free(src);
}

static void test_pipeline_source_empty(void) {
    PipelineStage *src = make_int_source(0);

    Value *v = src->next(src);
    ASSERT_NULL(v);

    pipeline_stage_free(src);
}

static void test_pipeline_filter_double(void) {
    PipelineStage *src = make_int_source(4);
    PipelineStage *dbl = make_double_filter(src);

    // Should get 0, 2, 4, 6
    for (int i = 0; i < 4; i++) {
        Value *v = dbl->next(dbl);
        ASSERT_NOT_NULL(v);
        ASSERT(v->integer == i * 2);
        value_free(v);
    }

    Value *v = dbl->next(dbl);
    ASSERT_NULL(v);

    pipeline_stage_free(dbl); // Frees src too
}

static void test_pipeline_filter_even(void) {
    PipelineStage *src = make_int_source(6);   // 0,1,2,3,4,5
    PipelineStage *evn = make_even_filter(src);

    // Should get 0, 2, 4
    int expected[] = {0, 2, 4};
    for (int i = 0; i < 3; i++) {
        Value *v = evn->next(evn);
        ASSERT_NOT_NULL(v);
        ASSERT(v->integer == expected[i]);
        value_free(v);
    }

    Value *v = evn->next(evn);
    ASSERT_NULL(v);

    pipeline_stage_free(evn);
}

static void test_pipeline_chained_filters(void) {
    // Source: 0..7 → even filter (0,2,4,6) → double (0,4,8,12)
    PipelineStage *src = make_int_source(8);
    PipelineStage *evn = make_even_filter(src);
    PipelineStage *dbl = make_double_filter(evn);

    int expected[] = {0, 4, 8, 12};
    for (int i = 0; i < 4; i++) {
        Value *v = dbl->next(dbl);
        ASSERT_NOT_NULL(v);
        ASSERT(v->integer == expected[i]);
        value_free(v);
    }

    Value *v = dbl->next(dbl);
    ASSERT_NULL(v);

    pipeline_stage_free(dbl); // Frees entire chain
}

static void test_pipeline_lazy_evaluation(void) {
    // Verify laziness: source only advances when downstream pulls.
    // Pull only 2 values from a source of 100.
    PipelineStage *src = make_int_source(100);

    Value *v0 = src->next(src);
    ASSERT_NOT_NULL(v0);
    ASSERT(v0->integer == 0);
    value_free(v0);

    Value *v1 = src->next(src);
    ASSERT_NOT_NULL(v1);
    ASSERT(v1->integer == 1);
    value_free(v1);

    // Source state should be at 2, not 100
    IntSourceState *s = src->state;
    ASSERT_INT_EQ(s->current, 2);

    pipeline_stage_free(src);
}

static void test_pipeline_drain_ints(void) {
    PipelineStage *src = make_int_source(3);

    FILE *f = tmpfile();
    ASSERT_NOT_NULL(f);
    pipeline_stage_drain(src, f); // Consumes and frees src

    rewind(f);
    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    ASSERT(strstr(buf, "0\n") != NULL);
    ASSERT(strstr(buf, "1\n") != NULL);
    ASSERT(strstr(buf, "2\n") != NULL);
}

static void test_pipeline_drain_table(void) {
    PipelineStage *src = make_table_source();

    FILE *f = tmpfile();
    ASSERT_NOT_NULL(f);
    pipeline_stage_drain(src, f);

    rewind(f);
    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    // Should contain pretty-printed table with header and data
    ASSERT(strstr(buf, "name") != NULL);
    ASSERT(strstr(buf, "val") != NULL);
    ASSERT(strstr(buf, "alpha") != NULL);
    ASSERT(strstr(buf, "beta") != NULL);
    ASSERT(strstr(buf, "10") != NULL);
    ASSERT(strstr(buf, "20") != NULL);
    // Separator
    ASSERT(strstr(buf, "\xe2\x94\x80") != NULL);
}

static void test_pipeline_drain_empty(void) {
    PipelineStage *src = make_int_source(0);

    FILE *f = tmpfile();
    ASSERT_NOT_NULL(f);
    pipeline_stage_drain(src, f);

    rewind(f);
    char buf[64];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    // Nothing written
    ASSERT(n == 0);
}

static void test_pipeline_drain_null(void) {
    // Should not crash
    pipeline_stage_drain(NULL, stdout);
}

static void test_pipeline_free_null(void) {
    // Should not crash
    pipeline_stage_free(NULL);
}

static void test_pipeline_filter_on_empty(void) {
    PipelineStage *src = make_int_source(0);
    PipelineStage *dbl = make_double_filter(src);

    Value *v = dbl->next(dbl);
    ASSERT_NULL(v);

    pipeline_stage_free(dbl);
}


int main(void) {
    printf("test_pipeline:\n");

    test_pipeline_source_basic();
    test_pipeline_source_empty();
    test_pipeline_filter_double();
    test_pipeline_filter_even();
    test_pipeline_chained_filters();
    test_pipeline_lazy_evaluation();
    test_pipeline_drain_ints();
    test_pipeline_drain_table();
    test_pipeline_drain_empty();
    test_pipeline_drain_null();
    test_pipeline_free_null();
    test_pipeline_filter_on_empty();

    TEST_REPORT();
}
