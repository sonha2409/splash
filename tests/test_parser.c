#include "test.h"
#include "tokenizer.h"
#include "parser.h"
#include "command.h"

static void test_parser_empty_input(void) {
    TokenList *tokens = tokenizer_tokenize("");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NULL(pl);
    token_list_free(tokens);
}

static void test_parser_single_command(void) {
    TokenList *tokens = tokenizer_tokenize("ls");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->num_commands, 1);
    ASSERT_INT_EQ(pl->commands[0]->argc, 1);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "ls");
    ASSERT_NULL(pl->commands[0]->argv[1]);
    ASSERT_INT_EQ(pl->background, 0);
    pipeline_free(pl);
    token_list_free(tokens);
}

static void test_parser_command_with_args(void) {
    TokenList *tokens = tokenizer_tokenize("ls -al /tmp");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->num_commands, 1);
    ASSERT_INT_EQ(pl->commands[0]->argc, 3);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "ls");
    ASSERT_STR_EQ(pl->commands[0]->argv[1], "-al");
    ASSERT_STR_EQ(pl->commands[0]->argv[2], "/tmp");
    ASSERT_NULL(pl->commands[0]->argv[3]);
    pipeline_free(pl);
    token_list_free(tokens);
}

static void test_parser_two_stage_pipeline(void) {
    TokenList *tokens = tokenizer_tokenize("ls | grep foo");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->num_commands, 2);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "ls");
    ASSERT_STR_EQ(pl->commands[1]->argv[0], "grep");
    ASSERT_STR_EQ(pl->commands[1]->argv[1], "foo");
    pipeline_free(pl);
    token_list_free(tokens);
}

static void test_parser_three_stage_pipeline(void) {
    TokenList *tokens = tokenizer_tokenize("ls | grep c | wc -l");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->num_commands, 3);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "ls");
    ASSERT_STR_EQ(pl->commands[1]->argv[0], "grep");
    ASSERT_STR_EQ(pl->commands[2]->argv[0], "wc");
    ASSERT_STR_EQ(pl->commands[2]->argv[1], "-l");
    pipeline_free(pl);
    token_list_free(tokens);
}

static void test_parser_background(void) {
    TokenList *tokens = tokenizer_tokenize("sleep 10 &");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->background, 1);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "sleep");
    pipeline_free(pl);
    token_list_free(tokens);
}

static void test_parser_error_pipe_at_start(void) {
    TokenList *tokens = tokenizer_tokenize("| ls");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NULL(pl);
    token_list_free(tokens);
}

static void test_parser_error_pipe_at_end(void) {
    TokenList *tokens = tokenizer_tokenize("ls |");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NULL(pl);
    token_list_free(tokens);
}

static void test_parser_error_double_pipe_no_cmd(void) {
    TokenList *tokens = tokenizer_tokenize("ls | | grep");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NULL(pl);
    token_list_free(tokens);
}

static void test_parser_incomplete_input(void) {
    TokenList *tokens = tokenizer_tokenize("echo \"unterminated");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NULL(pl); // no crash, no error message
    token_list_free(tokens);
}

static void test_parser_quoted_args(void) {
    TokenList *tokens = tokenizer_tokenize("echo \"hello world\" foo");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->commands[0]->argc, 3);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "echo");
    ASSERT_STR_EQ(pl->commands[0]->argv[1], "hello world");
    ASSERT_STR_EQ(pl->commands[0]->argv[2], "foo");
    pipeline_free(pl);
    token_list_free(tokens);
}

static void test_parser_absolute_path_command(void) {
    TokenList *tokens = tokenizer_tokenize("/bin/echo hello");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "/bin/echo");
    ASSERT_STR_EQ(pl->commands[0]->argv[1], "hello");
    pipeline_free(pl);
    token_list_free(tokens);
}

int main(void) {
    printf("test_parser\n");

    test_parser_empty_input();
    test_parser_single_command();
    test_parser_command_with_args();
    test_parser_two_stage_pipeline();
    test_parser_three_stage_pipeline();
    test_parser_background();
    test_parser_error_pipe_at_start();
    test_parser_error_pipe_at_end();
    test_parser_error_double_pipe_no_cmd();
    test_parser_incomplete_input();
    test_parser_quoted_args();
    test_parser_absolute_path_command();

    TEST_REPORT();
}
