#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "builtins.h"
#include "command.h"
#include "pipeline.h"
#include "table.h"
#include "test.h"
#include "util.h"
#include "value.h"


// Helper: write data to a pipe and return the read end fd.
static int make_input_fd(const char *data) {
    int pfd[2];
    if (pipe(pfd) != 0) return -1;

    ssize_t len = (ssize_t)strlen(data);
    ssize_t w = write(pfd[1], data, (size_t)len);
    (void)w;
    close(pfd[1]);
    return pfd[0];
}

// Helper: make a SimpleCommand with just a command name.
static SimpleCommand *make_cmd(const char *name) {
    SimpleCommand *cmd = simple_command_new();
    simple_command_add_arg(cmd, name);
    return cmd;
}


// ===== from-lines tests =====

static void test_from_lines_basic(void) {
    int fd = make_input_fd("hello\nworld\nfoo\n");
    SimpleCommand *cmd = make_cmd("from-lines");

    PipelineStage *stage = builtin_create_from_stage(cmd, fd);
    ASSERT_NOT_NULL(stage);

    Value *v = stage->next(stage);
    ASSERT_NOT_NULL(v);
    ASSERT(v->type == VALUE_TABLE);

    Table *t = v->table;
    ASSERT(t->col_count == 1);
    ASSERT_STR_EQ(t->columns[0].name, "line");
    ASSERT(t->row_count == 3);

    Value *cell0 = table_get(t, 0, 0);
    ASSERT_STR_EQ(cell0->string, "hello");

    Value *cell1 = table_get(t, 1, 0);
    ASSERT_STR_EQ(cell1->string, "world");

    Value *cell2 = table_get(t, 2, 0);
    ASSERT_STR_EQ(cell2->string, "foo");

    value_free(v);

    // Second call should return NULL
    Value *v2 = stage->next(stage);
    ASSERT_NULL(v2);

    pipeline_stage_free(stage);
    simple_command_free(cmd);
}

static void test_from_lines_no_trailing_newline(void) {
    int fd = make_input_fd("alpha\nbeta");
    SimpleCommand *cmd = make_cmd("from-lines");

    PipelineStage *stage = builtin_create_from_stage(cmd, fd);
    ASSERT_NOT_NULL(stage);

    Value *v = stage->next(stage);
    ASSERT_NOT_NULL(v);
    ASSERT(v->table->row_count == 2);

    ASSERT_STR_EQ(table_get(v->table, 0, 0)->string, "alpha");
    ASSERT_STR_EQ(table_get(v->table, 1, 0)->string, "beta");

    value_free(v);
    pipeline_stage_free(stage);
    simple_command_free(cmd);
}

static void test_from_lines_empty(void) {
    int fd = make_input_fd("");
    SimpleCommand *cmd = make_cmd("from-lines");

    PipelineStage *stage = builtin_create_from_stage(cmd, fd);
    ASSERT_NOT_NULL(stage);

    Value *v = stage->next(stage);
    ASSERT_NOT_NULL(v);
    ASSERT(v->type == VALUE_TABLE);
    ASSERT(v->table->row_count == 0);

    value_free(v);
    pipeline_stage_free(stage);
    simple_command_free(cmd);
}

static void test_from_lines_single_line(void) {
    int fd = make_input_fd("only one line\n");
    SimpleCommand *cmd = make_cmd("from-lines");

    PipelineStage *stage = builtin_create_from_stage(cmd, fd);
    ASSERT_NOT_NULL(stage);

    Value *v = stage->next(stage);
    ASSERT_NOT_NULL(v);
    ASSERT(v->table->row_count == 1);
    ASSERT_STR_EQ(table_get(v->table, 0, 0)->string, "only one line");

    value_free(v);
    pipeline_stage_free(stage);
    simple_command_free(cmd);
}


// ===== from-csv tests =====

static void test_from_csv_basic(void) {
    int fd = make_input_fd("name,age,city\nAlice,30,NYC\nBob,25,LA\n");
    SimpleCommand *cmd = make_cmd("from-csv");

    PipelineStage *stage = builtin_create_from_stage(cmd, fd);
    ASSERT_NOT_NULL(stage);

    Value *v = stage->next(stage);
    ASSERT_NOT_NULL(v);
    ASSERT(v->type == VALUE_TABLE);

    Table *t = v->table;
    ASSERT(t->col_count == 3);
    ASSERT_STR_EQ(t->columns[0].name, "name");
    ASSERT_STR_EQ(t->columns[1].name, "age");
    ASSERT_STR_EQ(t->columns[2].name, "city");
    ASSERT(t->row_count == 2);

    // Check data types — age should be inferred as int
    Value *age0 = table_get(t, 0, 1);
    ASSERT(age0->type == VALUE_INT);
    ASSERT(age0->integer == 30);

    Value *age1 = table_get(t, 1, 1);
    ASSERT(age1->type == VALUE_INT);
    ASSERT(age1->integer == 25);

    // name is string
    ASSERT_STR_EQ(table_get(t, 0, 0)->string, "Alice");
    ASSERT_STR_EQ(table_get(t, 1, 0)->string, "Bob");

    // Column type hint should be updated
    ASSERT(t->columns[1].type == VALUE_INT);

    value_free(v);
    pipeline_stage_free(stage);
    simple_command_free(cmd);
}

static void test_from_csv_quoted_fields(void) {
    int fd = make_input_fd("name,desc\n\"Smith, John\",\"He said \"\"hello\"\"\"\n");
    SimpleCommand *cmd = make_cmd("from-csv");

    PipelineStage *stage = builtin_create_from_stage(cmd, fd);
    ASSERT_NOT_NULL(stage);

    Value *v = stage->next(stage);
    ASSERT_NOT_NULL(v);
    ASSERT(v->table->row_count == 1);

    ASSERT_STR_EQ(table_get(v->table, 0, 0)->string, "Smith, John");
    ASSERT_STR_EQ(table_get(v->table, 0, 1)->string, "He said \"hello\"");

    value_free(v);
    pipeline_stage_free(stage);
    simple_command_free(cmd);
}

static void test_from_csv_float_inference(void) {
    int fd = make_input_fd("val\n1.5\n2.7\n3.14\n");
    SimpleCommand *cmd = make_cmd("from-csv");

    PipelineStage *stage = builtin_create_from_stage(cmd, fd);
    ASSERT_NOT_NULL(stage);

    Value *v = stage->next(stage);
    ASSERT_NOT_NULL(v);

    ASSERT(table_get(v->table, 0, 0)->type == VALUE_FLOAT);
    ASSERT(table_get(v->table, 0, 0)->floating == 1.5);
    ASSERT(v->table->columns[0].type == VALUE_FLOAT);

    value_free(v);
    pipeline_stage_free(stage);
    simple_command_free(cmd);
}

static void test_from_csv_empty(void) {
    int fd = make_input_fd("");
    SimpleCommand *cmd = make_cmd("from-csv");

    PipelineStage *stage = builtin_create_from_stage(cmd, fd);
    ASSERT_NOT_NULL(stage);

    Value *v = stage->next(stage);
    ASSERT_NULL(v);

    pipeline_stage_free(stage);
    simple_command_free(cmd);
}

static void test_from_csv_header_only(void) {
    int fd = make_input_fd("a,b,c\n");
    SimpleCommand *cmd = make_cmd("from-csv");

    PipelineStage *stage = builtin_create_from_stage(cmd, fd);
    ASSERT_NOT_NULL(stage);

    Value *v = stage->next(stage);
    ASSERT_NOT_NULL(v);
    ASSERT(v->table->col_count == 3);
    ASSERT(v->table->row_count == 0);

    value_free(v);
    pipeline_stage_free(stage);
    simple_command_free(cmd);
}

static void test_from_csv_fewer_fields(void) {
    // Row has fewer fields than header — padded with empty string
    int fd = make_input_fd("a,b,c\n1\n");
    SimpleCommand *cmd = make_cmd("from-csv");

    PipelineStage *stage = builtin_create_from_stage(cmd, fd);
    ASSERT_NOT_NULL(stage);

    Value *v = stage->next(stage);
    ASSERT_NOT_NULL(v);
    ASSERT(v->table->row_count == 1);
    ASSERT(table_get(v->table, 0, 0)->type == VALUE_INT);
    ASSERT(table_get(v->table, 0, 0)->integer == 1);
    ASSERT_STR_EQ(table_get(v->table, 0, 1)->string, "");
    ASSERT_STR_EQ(table_get(v->table, 0, 2)->string, "");

    value_free(v);
    pipeline_stage_free(stage);
    simple_command_free(cmd);
}

static void test_from_csv_bool_inference(void) {
    int fd = make_input_fd("flag\ntrue\nfalse\ntrue\n");
    SimpleCommand *cmd = make_cmd("from-csv");

    PipelineStage *stage = builtin_create_from_stage(cmd, fd);
    ASSERT_NOT_NULL(stage);

    Value *v = stage->next(stage);
    ASSERT_NOT_NULL(v);
    ASSERT(table_get(v->table, 0, 0)->type == VALUE_BOOL);
    ASSERT(table_get(v->table, 0, 0)->boolean == true);
    ASSERT(table_get(v->table, 1, 0)->type == VALUE_BOOL);
    ASSERT(table_get(v->table, 1, 0)->boolean == false);

    value_free(v);
    pipeline_stage_free(stage);
    simple_command_free(cmd);
}


// ===== from-json tests =====

static void test_from_json_basic(void) {
    const char *json = "[{\"name\":\"Alice\",\"age\":30},{\"name\":\"Bob\",\"age\":25}]";
    int fd = make_input_fd(json);
    SimpleCommand *cmd = make_cmd("from-json");

    PipelineStage *stage = builtin_create_from_stage(cmd, fd);
    ASSERT_NOT_NULL(stage);

    Value *v = stage->next(stage);
    ASSERT_NOT_NULL(v);
    ASSERT(v->type == VALUE_TABLE);

    Table *t = v->table;
    ASSERT(t->col_count == 2);
    ASSERT_STR_EQ(t->columns[0].name, "name");
    ASSERT_STR_EQ(t->columns[1].name, "age");
    ASSERT(t->row_count == 2);

    ASSERT_STR_EQ(table_get(t, 0, 0)->string, "Alice");
    ASSERT(table_get(t, 0, 1)->type == VALUE_INT);
    ASSERT(table_get(t, 0, 1)->integer == 30);

    ASSERT_STR_EQ(table_get(t, 1, 0)->string, "Bob");
    ASSERT(table_get(t, 1, 1)->integer == 25);

    // Column type hint
    ASSERT(t->columns[1].type == VALUE_INT);

    value_free(v);
    pipeline_stage_free(stage);
    simple_command_free(cmd);
}

static void test_from_json_bool_and_null(void) {
    const char *json = "[{\"name\":\"x\",\"active\":true,\"note\":null}]";
    int fd = make_input_fd(json);
    SimpleCommand *cmd = make_cmd("from-json");

    PipelineStage *stage = builtin_create_from_stage(cmd, fd);
    ASSERT_NOT_NULL(stage);

    Value *v = stage->next(stage);
    ASSERT_NOT_NULL(v);

    ASSERT(table_get(v->table, 0, 1)->type == VALUE_BOOL);
    ASSERT(table_get(v->table, 0, 1)->boolean == true);
    ASSERT(table_get(v->table, 0, 2)->type == VALUE_NIL);

    value_free(v);
    pipeline_stage_free(stage);
    simple_command_free(cmd);
}

static void test_from_json_mixed_keys(void) {
    // Objects have different keys — missing keys become NIL
    const char *json = "[{\"a\":1,\"b\":2},{\"b\":3,\"c\":4}]";
    int fd = make_input_fd(json);
    SimpleCommand *cmd = make_cmd("from-json");

    PipelineStage *stage = builtin_create_from_stage(cmd, fd);
    ASSERT_NOT_NULL(stage);

    Value *v = stage->next(stage);
    ASSERT_NOT_NULL(v);

    Table *t = v->table;
    ASSERT(t->col_count == 3);  // a, b, c
    ASSERT_STR_EQ(t->columns[0].name, "a");
    ASSERT_STR_EQ(t->columns[1].name, "b");
    ASSERT_STR_EQ(t->columns[2].name, "c");

    // Row 0: a=1, b=2, c=NIL
    ASSERT(table_get(t, 0, 0)->integer == 1);
    ASSERT(table_get(t, 0, 1)->integer == 2);
    ASSERT(table_get(t, 0, 2)->type == VALUE_NIL);

    // Row 1: a=NIL, b=3, c=4
    ASSERT(table_get(t, 1, 0)->type == VALUE_NIL);
    ASSERT(table_get(t, 1, 1)->integer == 3);
    ASSERT(table_get(t, 1, 2)->integer == 4);

    value_free(v);
    pipeline_stage_free(stage);
    simple_command_free(cmd);
}

static void test_from_json_empty_array(void) {
    int fd = make_input_fd("[]");
    SimpleCommand *cmd = make_cmd("from-json");

    PipelineStage *stage = builtin_create_from_stage(cmd, fd);
    ASSERT_NOT_NULL(stage);

    Value *v = stage->next(stage);
    ASSERT_NULL(v);

    pipeline_stage_free(stage);
    simple_command_free(cmd);
}

static void test_from_json_floats(void) {
    const char *json = "[{\"temp\":72.5},{\"temp\":68.3}]";
    int fd = make_input_fd(json);
    SimpleCommand *cmd = make_cmd("from-json");

    PipelineStage *stage = builtin_create_from_stage(cmd, fd);
    ASSERT_NOT_NULL(stage);

    Value *v = stage->next(stage);
    ASSERT_NOT_NULL(v);

    ASSERT(table_get(v->table, 0, 0)->type == VALUE_FLOAT);
    ASSERT(table_get(v->table, 0, 0)->floating == 72.5);

    value_free(v);
    pipeline_stage_free(stage);
    simple_command_free(cmd);
}

static void test_from_json_nested_stringified(void) {
    // Nested objects/arrays should be stringified
    const char *json = "[{\"name\":\"x\",\"tags\":[1,2,3]}]";
    int fd = make_input_fd(json);
    SimpleCommand *cmd = make_cmd("from-json");

    PipelineStage *stage = builtin_create_from_stage(cmd, fd);
    ASSERT_NOT_NULL(stage);

    Value *v = stage->next(stage);
    ASSERT_NOT_NULL(v);
    ASSERT(v->table->col_count == 2);

    // tags should be a string containing the JSON array text
    Value *tags = table_get(v->table, 0, 1);
    ASSERT(tags->type == VALUE_STRING);
    ASSERT_STR_EQ(tags->string, "[1,2,3]");

    value_free(v);
    pipeline_stage_free(stage);
    simple_command_free(cmd);
}

static void test_from_json_string_escapes(void) {
    const char *json = "[{\"msg\":\"hello\\nworld\"}]";
    int fd = make_input_fd(json);
    SimpleCommand *cmd = make_cmd("from-json");

    PipelineStage *stage = builtin_create_from_stage(cmd, fd);
    ASSERT_NOT_NULL(stage);

    Value *v = stage->next(stage);
    ASSERT_NOT_NULL(v);
    ASSERT_STR_EQ(table_get(v->table, 0, 0)->string, "hello\nworld");

    value_free(v);
    pipeline_stage_free(stage);
    simple_command_free(cmd);
}


// ===== Helper: make a table source stage for to-csv/to-json tests =====

typedef struct {
    Value *table_val;
    int yielded;
} MockTableState;

static Value *mock_table_next(PipelineStage *self) {
    MockTableState *s = self->state;
    if (s->yielded || !s->table_val) return NULL;
    s->yielded = 1;
    return value_clone(s->table_val);
}

static void mock_table_free(PipelineStage *self) {
    MockTableState *s = self->state;
    if (s->table_val) value_free(s->table_val);
    free(s);
}

static PipelineStage *make_table_stage(Table *t) {
    MockTableState *s = xmalloc(sizeof(MockTableState));
    s->table_val = t ? value_table(t) : NULL;
    s->yielded = 0;
    return pipeline_stage_new(mock_table_next, mock_table_free, s, NULL);
}

// Helper: drain a stage to a string.
static char *drain_to_string(PipelineStage *stage) {
    FILE *f = tmpfile();
    if (!f) return NULL;
    pipeline_stage_drain(stage, f);

    long sz = ftell(f);
    rewind(f);
    char *buf = xmalloc((size_t)sz + 1);
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}


// ===== to-csv tests =====

static void test_to_csv_basic(void) {
    const char *cols[] = {"name", "age"};
    ValueType types[] = {VALUE_STRING, VALUE_INT};
    Table *t = table_new(cols, types, 2);

    Value *r1[] = {value_string("Alice"), value_int(30)};
    table_add_row(t, r1, 2);
    Value *r2[] = {value_string("Bob"), value_int(25)};
    table_add_row(t, r2, 2);

    PipelineStage *src = make_table_stage(t);
    SimpleCommand *cmd = make_cmd("to-csv");
    PipelineStage *stage = builtin_create_stage(cmd, src);
    ASSERT_NOT_NULL(stage);

    char *out = drain_to_string(stage);
    ASSERT(strstr(out, "name,age") != NULL);
    ASSERT(strstr(out, "Alice,30") != NULL);
    ASSERT(strstr(out, "Bob,25") != NULL);
    free(out);
    simple_command_free(cmd);
}

static void test_to_csv_quoting(void) {
    // Fields with commas and quotes need quoting
    const char *cols[] = {"msg"};
    ValueType types[] = {VALUE_STRING};
    Table *t = table_new(cols, types, 1);

    Value *r1[] = {value_string("hello, world")};
    table_add_row(t, r1, 1);
    Value *r2[] = {value_string("say \"hi\"")};
    table_add_row(t, r2, 1);

    PipelineStage *src = make_table_stage(t);
    SimpleCommand *cmd = make_cmd("to-csv");
    PipelineStage *stage = builtin_create_stage(cmd, src);
    ASSERT_NOT_NULL(stage);

    char *out = drain_to_string(stage);
    ASSERT(strstr(out, "\"hello, world\"") != NULL);
    ASSERT(strstr(out, "\"say \"\"hi\"\"\"") != NULL);
    free(out);
    simple_command_free(cmd);
}

static void test_to_csv_nil_values(void) {
    const char *cols[] = {"a", "b"};
    ValueType types[] = {VALUE_STRING, VALUE_INT};
    Table *t = table_new(cols, types, 2);

    Value *r1[] = {value_string("x"), value_nil()};
    table_add_row(t, r1, 2);

    PipelineStage *src = make_table_stage(t);
    SimpleCommand *cmd = make_cmd("to-csv");
    PipelineStage *stage = builtin_create_stage(cmd, src);
    ASSERT_NOT_NULL(stage);

    char *out = drain_to_string(stage);
    // NIL should produce empty field: "x,"
    ASSERT(strstr(out, "x,") != NULL);
    // Should NOT contain "nil"
    ASSERT(strstr(out, "nil") == NULL);
    free(out);
    simple_command_free(cmd);
}

static void test_to_csv_empty_table(void) {
    const char *cols[] = {"col1", "col2"};
    ValueType types[] = {VALUE_STRING, VALUE_STRING};
    Table *t = table_new(cols, types, 2);

    PipelineStage *src = make_table_stage(t);
    SimpleCommand *cmd = make_cmd("to-csv");
    PipelineStage *stage = builtin_create_stage(cmd, src);
    ASSERT_NOT_NULL(stage);

    char *out = drain_to_string(stage);
    // Header only
    ASSERT(strstr(out, "col1,col2") != NULL);
    free(out);
    simple_command_free(cmd);
}

static void test_to_csv_roundtrip(void) {
    // from-csv → to-csv should produce equivalent CSV
    const char *csv = "name,score\nAlice,95\nBob,87\n";
    int fd = make_input_fd(csv);
    SimpleCommand *from_cmd = make_cmd("from-csv");
    PipelineStage *src = builtin_create_from_stage(from_cmd, fd);
    ASSERT_NOT_NULL(src);

    SimpleCommand *to_cmd = make_cmd("to-csv");
    PipelineStage *stage = builtin_create_stage(to_cmd, src);
    ASSERT_NOT_NULL(stage);

    char *out = drain_to_string(stage);
    ASSERT(strstr(out, "name,score") != NULL);
    ASSERT(strstr(out, "Alice,95") != NULL);
    ASSERT(strstr(out, "Bob,87") != NULL);
    free(out);
    simple_command_free(from_cmd);
    simple_command_free(to_cmd);
}

static void test_to_csv_bool_float(void) {
    const char *cols[] = {"flag", "val"};
    ValueType types[] = {VALUE_BOOL, VALUE_FLOAT};
    Table *t = table_new(cols, types, 2);

    Value *r1[] = {value_bool(true), value_float(3.14)};
    table_add_row(t, r1, 2);

    PipelineStage *src = make_table_stage(t);
    SimpleCommand *cmd = make_cmd("to-csv");
    PipelineStage *stage = builtin_create_stage(cmd, src);
    ASSERT_NOT_NULL(stage);

    char *out = drain_to_string(stage);
    ASSERT(strstr(out, "true") != NULL);
    ASSERT(strstr(out, "3.14") != NULL);
    free(out);
    simple_command_free(cmd);
}


// ===== to-json tests =====

static void test_to_json_basic(void) {
    const char *cols[] = {"name", "age"};
    ValueType types[] = {VALUE_STRING, VALUE_INT};
    Table *t = table_new(cols, types, 2);

    Value *r1[] = {value_string("Alice"), value_int(30)};
    table_add_row(t, r1, 2);
    Value *r2[] = {value_string("Bob"), value_int(25)};
    table_add_row(t, r2, 2);

    PipelineStage *src = make_table_stage(t);
    SimpleCommand *cmd = make_cmd("to-json");
    PipelineStage *stage = builtin_create_stage(cmd, src);
    ASSERT_NOT_NULL(stage);

    char *out = drain_to_string(stage);
    ASSERT(strstr(out, "\"name\": \"Alice\"") != NULL);
    ASSERT(strstr(out, "\"age\": 30") != NULL);
    ASSERT(strstr(out, "\"name\": \"Bob\"") != NULL);
    ASSERT(strstr(out, "\"age\": 25") != NULL);
    // Should be a JSON array
    ASSERT(out[0] == '[');
    ASSERT(strstr(out, "]") != NULL);
    free(out);
    simple_command_free(cmd);
}

static void test_to_json_nil_and_bool(void) {
    const char *cols[] = {"flag", "val"};
    ValueType types[] = {VALUE_BOOL, VALUE_STRING};
    Table *t = table_new(cols, types, 2);

    Value *r1[] = {value_bool(true), value_nil()};
    table_add_row(t, r1, 2);

    PipelineStage *src = make_table_stage(t);
    SimpleCommand *cmd = make_cmd("to-json");
    PipelineStage *stage = builtin_create_stage(cmd, src);
    ASSERT_NOT_NULL(stage);

    char *out = drain_to_string(stage);
    ASSERT(strstr(out, "true") != NULL);
    ASSERT(strstr(out, "null") != NULL);
    free(out);
    simple_command_free(cmd);
}

static void test_to_json_empty_table(void) {
    const char *cols[] = {"x"};
    ValueType types[] = {VALUE_STRING};
    Table *t = table_new(cols, types, 1);

    PipelineStage *src = make_table_stage(t);
    SimpleCommand *cmd = make_cmd("to-json");
    PipelineStage *stage = builtin_create_stage(cmd, src);
    ASSERT_NOT_NULL(stage);

    char *out = drain_to_string(stage);
    // Should be empty array
    ASSERT(strstr(out, "[\n]") != NULL);
    free(out);
    simple_command_free(cmd);
}

static void test_to_json_string_escaping(void) {
    const char *cols[] = {"msg"};
    ValueType types[] = {VALUE_STRING};
    Table *t = table_new(cols, types, 1);

    Value *r1[] = {value_string("line1\nline2")};
    table_add_row(t, r1, 1);
    Value *r2[] = {value_string("say \"hello\"")};
    table_add_row(t, r2, 1);

    PipelineStage *src = make_table_stage(t);
    SimpleCommand *cmd = make_cmd("to-json");
    PipelineStage *stage = builtin_create_stage(cmd, src);
    ASSERT_NOT_NULL(stage);

    char *out = drain_to_string(stage);
    ASSERT(strstr(out, "\\n") != NULL);
    ASSERT(strstr(out, "\\\"hello\\\"") != NULL);
    free(out);
    simple_command_free(cmd);
}

static void test_to_json_roundtrip(void) {
    // from-json → to-json should produce valid JSON with same data
    const char *json = "[{\"name\":\"Alice\",\"age\":30},{\"name\":\"Bob\",\"age\":25}]";
    int fd = make_input_fd(json);
    SimpleCommand *from_cmd = make_cmd("from-json");
    PipelineStage *src = builtin_create_from_stage(from_cmd, fd);
    ASSERT_NOT_NULL(src);

    SimpleCommand *to_cmd = make_cmd("to-json");
    PipelineStage *stage = builtin_create_stage(to_cmd, src);
    ASSERT_NOT_NULL(stage);

    char *out = drain_to_string(stage);
    ASSERT(strstr(out, "\"name\": \"Alice\"") != NULL);
    ASSERT(strstr(out, "\"age\": 30") != NULL);
    ASSERT(strstr(out, "\"name\": \"Bob\"") != NULL);
    ASSERT(strstr(out, "\"age\": 25") != NULL);
    free(out);
    simple_command_free(from_cmd);
    simple_command_free(to_cmd);
}

static void test_to_json_floats(void) {
    const char *cols[] = {"val"};
    ValueType types[] = {VALUE_FLOAT};
    Table *t = table_new(cols, types, 1);

    Value *r1[] = {value_float(3.14)};
    table_add_row(t, r1, 1);

    PipelineStage *src = make_table_stage(t);
    SimpleCommand *cmd = make_cmd("to-json");
    PipelineStage *stage = builtin_create_stage(cmd, src);
    ASSERT_NOT_NULL(stage);

    char *out = drain_to_string(stage);
    ASSERT(strstr(out, "3.14") != NULL);
    // Should NOT be quoted
    ASSERT(strstr(out, "\"3.14\"") == NULL);
    free(out);
    simple_command_free(cmd);
}

static void test_to_csv_json_registered(void) {
    ASSERT(builtin_is_structured("to-csv") == 1);
    ASSERT(builtin_is_structured("to-json") == 1);
}


// ===== is_from_source tests =====

static void test_is_from_source(void) {
    ASSERT(builtin_is_from_source("from-csv") == 1);
    ASSERT(builtin_is_from_source("from-json") == 1);
    ASSERT(builtin_is_from_source("from-lines") == 1);
    ASSERT(builtin_is_from_source("ls") == 0);
    ASSERT(builtin_is_from_source("where") == 0);
    ASSERT(builtin_is_from_source("from-xml") == 0);
}

static void test_is_structured_includes_from(void) {
    ASSERT(builtin_is_structured("from-csv") == 1);
    ASSERT(builtin_is_structured("from-json") == 1);
    ASSERT(builtin_is_structured("from-lines") == 1);
}


int main(void) {
    printf("test_from_stages:\n");

    // from-lines
    test_from_lines_basic();
    test_from_lines_no_trailing_newline();
    test_from_lines_empty();
    test_from_lines_single_line();

    // from-csv
    test_from_csv_basic();
    test_from_csv_quoted_fields();
    test_from_csv_float_inference();
    test_from_csv_empty();
    test_from_csv_header_only();
    test_from_csv_fewer_fields();
    test_from_csv_bool_inference();

    // from-json
    test_from_json_basic();
    test_from_json_bool_and_null();
    test_from_json_mixed_keys();
    test_from_json_empty_array();
    test_from_json_floats();
    test_from_json_nested_stringified();
    test_from_json_string_escapes();

    // to-csv
    test_to_csv_basic();
    test_to_csv_quoting();
    test_to_csv_nil_values();
    test_to_csv_empty_table();
    test_to_csv_roundtrip();
    test_to_csv_bool_float();

    // to-json
    test_to_json_basic();
    test_to_json_nil_and_bool();
    test_to_json_empty_table();
    test_to_json_string_escaping();
    test_to_json_roundtrip();
    test_to_json_floats();

    // Registration
    test_to_csv_json_registered();
    test_is_from_source();
    test_is_structured_includes_from();

    TEST_REPORT();
}
