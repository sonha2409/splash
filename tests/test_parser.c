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

static void test_parser_output_redirect(void) {
    TokenList *tokens = tokenizer_tokenize("ls > out.txt");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->num_commands, 1);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "ls");
    ASSERT_INT_EQ(pl->commands[0]->num_redirects, 1);
    ASSERT_INT_EQ(pl->commands[0]->redirects[0].type, REDIRECT_OUTPUT);
    ASSERT_STR_EQ(pl->commands[0]->redirects[0].target, "out.txt");
    pipeline_free(pl);
    token_list_free(tokens);
}

static void test_parser_append_redirect(void) {
    TokenList *tokens = tokenizer_tokenize("echo hi >> log.txt");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->commands[0]->num_redirects, 1);
    ASSERT_INT_EQ(pl->commands[0]->redirects[0].type, REDIRECT_APPEND);
    ASSERT_STR_EQ(pl->commands[0]->redirects[0].target, "log.txt");
    pipeline_free(pl);
    token_list_free(tokens);
}

static void test_parser_input_redirect(void) {
    TokenList *tokens = tokenizer_tokenize("cat < input.txt");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "cat");
    ASSERT_INT_EQ(pl->commands[0]->num_redirects, 1);
    ASSERT_INT_EQ(pl->commands[0]->redirects[0].type, REDIRECT_INPUT);
    ASSERT_STR_EQ(pl->commands[0]->redirects[0].target, "input.txt");
    pipeline_free(pl);
    token_list_free(tokens);
}

static void test_parser_stderr_redirect(void) {
    TokenList *tokens = tokenizer_tokenize("cmd 2> err.log");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->commands[0]->num_redirects, 1);
    ASSERT_INT_EQ(pl->commands[0]->redirects[0].type, REDIRECT_ERR);
    ASSERT_STR_EQ(pl->commands[0]->redirects[0].target, "err.log");
    pipeline_free(pl);
    token_list_free(tokens);
}

static void test_parser_out_err_redirect(void) {
    TokenList *tokens = tokenizer_tokenize("cmd >& all.log");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->commands[0]->num_redirects, 1);
    ASSERT_INT_EQ(pl->commands[0]->redirects[0].type, REDIRECT_OUT_ERR);
    pipeline_free(pl);
    token_list_free(tokens);
}

static void test_parser_append_err_redirect(void) {
    TokenList *tokens = tokenizer_tokenize("cmd >>& all.log");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->commands[0]->num_redirects, 1);
    ASSERT_INT_EQ(pl->commands[0]->redirects[0].type, REDIRECT_APPEND_ERR);
    pipeline_free(pl);
    token_list_free(tokens);
}

static void test_parser_multiple_redirects(void) {
    TokenList *tokens = tokenizer_tokenize("cmd < in.txt > out.txt 2> err.txt");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->commands[0]->num_redirects, 3);
    ASSERT_INT_EQ(pl->commands[0]->redirects[0].type, REDIRECT_INPUT);
    ASSERT_INT_EQ(pl->commands[0]->redirects[1].type, REDIRECT_OUTPUT);
    ASSERT_INT_EQ(pl->commands[0]->redirects[2].type, REDIRECT_ERR);
    pipeline_free(pl);
    token_list_free(tokens);
}

static void test_parser_redirect_before_args(void) {
    TokenList *tokens = tokenizer_tokenize("> out.txt ls -la");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "ls");
    ASSERT_STR_EQ(pl->commands[0]->argv[1], "-la");
    ASSERT_INT_EQ(pl->commands[0]->num_redirects, 1);
    ASSERT_INT_EQ(pl->commands[0]->redirects[0].type, REDIRECT_OUTPUT);
    pipeline_free(pl);
    token_list_free(tokens);
}

static void test_parser_redirect_between_args(void) {
    TokenList *tokens = tokenizer_tokenize("grep > out.txt pattern");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->commands[0]->argc, 2);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "grep");
    ASSERT_STR_EQ(pl->commands[0]->argv[1], "pattern");
    ASSERT_INT_EQ(pl->commands[0]->num_redirects, 1);
    pipeline_free(pl);
    token_list_free(tokens);
}

static void test_parser_redirect_missing_filename(void) {
    TokenList *tokens = tokenizer_tokenize("ls >");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NULL(pl);
    token_list_free(tokens);
}

static void test_parser_redirect_with_pipe(void) {
    TokenList *tokens = tokenizer_tokenize("ls 2> err.txt | grep foo > out.txt");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->num_commands, 2);
    ASSERT_INT_EQ(pl->commands[0]->num_redirects, 1);
    ASSERT_INT_EQ(pl->commands[0]->redirects[0].type, REDIRECT_ERR);
    ASSERT_INT_EQ(pl->commands[1]->num_redirects, 1);
    ASSERT_INT_EQ(pl->commands[1]->redirects[0].type, REDIRECT_OUTPUT);
    pipeline_free(pl);
    token_list_free(tokens);
}

static void test_parser_redirect_only_no_command(void) {
    TokenList *tokens = tokenizer_tokenize("> out.txt");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NULL(pl); // redirect without a command word
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

// --- Structured pipe |> tests ---

static void test_parser_structured_pipe_basic(void) {
    TokenList *tokens = tokenizer_tokenize("ls |> where size > 100");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->num_commands, 2);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "ls");
    ASSERT_STR_EQ(pl->commands[1]->argv[0], "where");
    ASSERT_INT_EQ(pl->pipe_types[0], PIPE_STRUCTURED);
    pipeline_free(pl);
    token_list_free(tokens);
}

static void test_parser_text_pipe_type(void) {
    TokenList *tokens = tokenizer_tokenize("ls | grep foo");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->num_commands, 2);
    ASSERT_INT_EQ(pl->pipe_types[0], PIPE_TEXT);
    pipeline_free(pl);
    token_list_free(tokens);
}

static void test_parser_mixed_pipes(void) {
    TokenList *tokens = tokenizer_tokenize("ls | grep c |> sort name");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->num_commands, 3);
    ASSERT_INT_EQ(pl->pipe_types[0], PIPE_TEXT);
    ASSERT_INT_EQ(pl->pipe_types[1], PIPE_STRUCTURED);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "ls");
    ASSERT_STR_EQ(pl->commands[1]->argv[0], "grep");
    ASSERT_STR_EQ(pl->commands[2]->argv[0], "sort");
    pipeline_free(pl);
    token_list_free(tokens);
}

static void test_parser_structured_pipe_chain(void) {
    TokenList *tokens = tokenizer_tokenize("ls |> where type == file |> sort size");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->num_commands, 3);
    ASSERT_INT_EQ(pl->pipe_types[0], PIPE_STRUCTURED);
    ASSERT_INT_EQ(pl->pipe_types[1], PIPE_STRUCTURED);
    pipeline_free(pl);
    token_list_free(tokens);
}

static void test_parser_structured_pipe_error_no_cmd(void) {
    TokenList *tokens = tokenizer_tokenize("ls |>");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NULL(pl);
    token_list_free(tokens);
}

static void test_parser_structured_pipe_at_start(void) {
    TokenList *tokens = tokenizer_tokenize("|> sort");
    Pipeline *pl = parser_parse(tokens);
    ASSERT_NULL(pl);
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
    test_parser_output_redirect();
    test_parser_append_redirect();
    test_parser_input_redirect();
    test_parser_stderr_redirect();
    test_parser_out_err_redirect();
    test_parser_append_err_redirect();
    test_parser_multiple_redirects();
    test_parser_redirect_before_args();
    test_parser_redirect_between_args();
    test_parser_redirect_missing_filename();
    test_parser_redirect_with_pipe();
    test_parser_redirect_only_no_command();
    test_parser_absolute_path_command();

    // Structured pipe |>
    test_parser_structured_pipe_basic();
    test_parser_text_pipe_type();
    test_parser_mixed_pipes();
    test_parser_structured_pipe_chain();
    test_parser_structured_pipe_error_no_cmd();
    test_parser_structured_pipe_at_start();

    TEST_REPORT();
}
