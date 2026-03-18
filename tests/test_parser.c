#include "test.h"
#include "tokenizer.h"
#include "parser.h"
#include "command.h"

// Helper: parse and extract the first (only) pipeline from a single-pipeline input.
// Returns NULL if parsing fails. Caller must free the CommandList via command_list_free.
static Pipeline *parse_single_pipeline(const char *input, CommandList **out_list) {
    TokenList *tokens = tokenizer_tokenize(input);
    CommandList *list = parser_parse(tokens, NULL);
    token_list_free(tokens);
    *out_list = list;
    if (!list || list->num_entries == 0) {
        return NULL;
    }
    return list->entries[0].pipeline;
}

static void test_parser_empty_input(void) {
    TokenList *tokens = tokenizer_tokenize("");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NULL(list);
    token_list_free(tokens);
}

static void test_parser_single_command(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("ls", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->num_commands, 1);
    ASSERT_INT_EQ(pl->commands[0]->argc, 1);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "ls");
    ASSERT_NULL(pl->commands[0]->argv[1]);
    ASSERT_INT_EQ(pl->background, 0);
    command_list_free(list);
}

static void test_parser_command_with_args(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("ls -al /tmp", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->num_commands, 1);
    ASSERT_INT_EQ(pl->commands[0]->argc, 3);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "ls");
    ASSERT_STR_EQ(pl->commands[0]->argv[1], "-al");
    ASSERT_STR_EQ(pl->commands[0]->argv[2], "/tmp");
    ASSERT_NULL(pl->commands[0]->argv[3]);
    command_list_free(list);
}

static void test_parser_two_stage_pipeline(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("ls | grep foo", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->num_commands, 2);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "ls");
    ASSERT_STR_EQ(pl->commands[1]->argv[0], "grep");
    ASSERT_STR_EQ(pl->commands[1]->argv[1], "foo");
    command_list_free(list);
}

static void test_parser_three_stage_pipeline(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("ls | grep c | wc -l", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->num_commands, 3);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "ls");
    ASSERT_STR_EQ(pl->commands[1]->argv[0], "grep");
    ASSERT_STR_EQ(pl->commands[2]->argv[0], "wc");
    ASSERT_STR_EQ(pl->commands[2]->argv[1], "-l");
    command_list_free(list);
}

static void test_parser_background(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("sleep 10 &", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->background, 1);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "sleep");
    command_list_free(list);
}

static void test_parser_error_pipe_at_start(void) {
    TokenList *tokens = tokenizer_tokenize("| ls");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NULL(list);
    token_list_free(tokens);
}

static void test_parser_error_pipe_at_end(void) {
    TokenList *tokens = tokenizer_tokenize("ls |");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NULL(list);
    token_list_free(tokens);
}

static void test_parser_error_double_pipe_no_cmd(void) {
    TokenList *tokens = tokenizer_tokenize("ls | | grep");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NULL(list);
    token_list_free(tokens);
}

static void test_parser_incomplete_input(void) {
    TokenList *tokens = tokenizer_tokenize("echo \"unterminated");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NULL(list); // no crash, no error message
    token_list_free(tokens);
}

static void test_parser_quoted_args(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("echo \"hello world\" foo", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->commands[0]->argc, 3);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "echo");
    ASSERT_STR_EQ(pl->commands[0]->argv[1], "hello world");
    ASSERT_STR_EQ(pl->commands[0]->argv[2], "foo");
    command_list_free(list);
}

static void test_parser_output_redirect(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("ls > out.txt", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->num_commands, 1);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "ls");
    ASSERT_INT_EQ(pl->commands[0]->num_redirects, 1);
    ASSERT_INT_EQ(pl->commands[0]->redirects[0].type, REDIRECT_OUTPUT);
    ASSERT_STR_EQ(pl->commands[0]->redirects[0].target, "out.txt");
    command_list_free(list);
}

static void test_parser_append_redirect(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("echo hi >> log.txt", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->commands[0]->num_redirects, 1);
    ASSERT_INT_EQ(pl->commands[0]->redirects[0].type, REDIRECT_APPEND);
    ASSERT_STR_EQ(pl->commands[0]->redirects[0].target, "log.txt");
    command_list_free(list);
}

static void test_parser_input_redirect(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("cat < input.txt", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "cat");
    ASSERT_INT_EQ(pl->commands[0]->num_redirects, 1);
    ASSERT_INT_EQ(pl->commands[0]->redirects[0].type, REDIRECT_INPUT);
    ASSERT_STR_EQ(pl->commands[0]->redirects[0].target, "input.txt");
    command_list_free(list);
}

static void test_parser_stderr_redirect(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("cmd 2> err.log", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->commands[0]->num_redirects, 1);
    ASSERT_INT_EQ(pl->commands[0]->redirects[0].type, REDIRECT_ERR);
    ASSERT_STR_EQ(pl->commands[0]->redirects[0].target, "err.log");
    command_list_free(list);
}

static void test_parser_out_err_redirect(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("cmd >& all.log", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->commands[0]->num_redirects, 1);
    ASSERT_INT_EQ(pl->commands[0]->redirects[0].type, REDIRECT_OUT_ERR);
    command_list_free(list);
}

static void test_parser_append_err_redirect(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("cmd >>& all.log", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->commands[0]->num_redirects, 1);
    ASSERT_INT_EQ(pl->commands[0]->redirects[0].type, REDIRECT_APPEND_ERR);
    command_list_free(list);
}

static void test_parser_multiple_redirects(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("cmd < in.txt > out.txt 2> err.txt", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->commands[0]->num_redirects, 3);
    ASSERT_INT_EQ(pl->commands[0]->redirects[0].type, REDIRECT_INPUT);
    ASSERT_INT_EQ(pl->commands[0]->redirects[1].type, REDIRECT_OUTPUT);
    ASSERT_INT_EQ(pl->commands[0]->redirects[2].type, REDIRECT_ERR);
    command_list_free(list);
}

static void test_parser_redirect_before_args(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("> out.txt ls -la", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "ls");
    ASSERT_STR_EQ(pl->commands[0]->argv[1], "-la");
    ASSERT_INT_EQ(pl->commands[0]->num_redirects, 1);
    ASSERT_INT_EQ(pl->commands[0]->redirects[0].type, REDIRECT_OUTPUT);
    command_list_free(list);
}

static void test_parser_redirect_between_args(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("grep > out.txt pattern", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->commands[0]->argc, 2);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "grep");
    ASSERT_STR_EQ(pl->commands[0]->argv[1], "pattern");
    ASSERT_INT_EQ(pl->commands[0]->num_redirects, 1);
    command_list_free(list);
}

static void test_parser_redirect_missing_filename(void) {
    TokenList *tokens = tokenizer_tokenize("ls >");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NULL(list);
    token_list_free(tokens);
}

static void test_parser_redirect_with_pipe(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("ls 2> err.txt | grep foo > out.txt", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->num_commands, 2);
    ASSERT_INT_EQ(pl->commands[0]->num_redirects, 1);
    ASSERT_INT_EQ(pl->commands[0]->redirects[0].type, REDIRECT_ERR);
    ASSERT_INT_EQ(pl->commands[1]->num_redirects, 1);
    ASSERT_INT_EQ(pl->commands[1]->redirects[0].type, REDIRECT_OUTPUT);
    command_list_free(list);
}

static void test_parser_redirect_only_no_command(void) {
    TokenList *tokens = tokenizer_tokenize("> out.txt");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NULL(list); // redirect without a command word
    token_list_free(tokens);
}

static void test_parser_absolute_path_command(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("/bin/echo hello", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "/bin/echo");
    ASSERT_STR_EQ(pl->commands[0]->argv[1], "hello");
    command_list_free(list);
}

// --- Structured pipe |> tests ---

static void test_parser_structured_pipe_basic(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("ls |> where size > 100", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->num_commands, 2);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "ls");
    ASSERT_STR_EQ(pl->commands[1]->argv[0], "where");
    ASSERT_INT_EQ(pl->pipe_types[0], PIPE_STRUCTURED);
    command_list_free(list);
}

static void test_parser_text_pipe_type(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("ls | grep foo", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->num_commands, 2);
    ASSERT_INT_EQ(pl->pipe_types[0], PIPE_TEXT);
    command_list_free(list);
}

static void test_parser_mixed_pipes(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("ls | grep c |> sort name", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->num_commands, 3);
    ASSERT_INT_EQ(pl->pipe_types[0], PIPE_TEXT);
    ASSERT_INT_EQ(pl->pipe_types[1], PIPE_STRUCTURED);
    ASSERT_STR_EQ(pl->commands[0]->argv[0], "ls");
    ASSERT_STR_EQ(pl->commands[1]->argv[0], "grep");
    ASSERT_STR_EQ(pl->commands[2]->argv[0], "sort");
    command_list_free(list);
}

static void test_parser_structured_pipe_chain(void) {
    CommandList *list;
    Pipeline *pl = parse_single_pipeline("ls |> where type == file |> sort size", &list);
    ASSERT_NOT_NULL(pl);
    ASSERT_INT_EQ(pl->num_commands, 3);
    ASSERT_INT_EQ(pl->pipe_types[0], PIPE_STRUCTURED);
    ASSERT_INT_EQ(pl->pipe_types[1], PIPE_STRUCTURED);
    command_list_free(list);
}

static void test_parser_structured_pipe_error_no_cmd(void) {
    TokenList *tokens = tokenizer_tokenize("ls |>");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NULL(list);
    token_list_free(tokens);
}

static void test_parser_structured_pipe_at_start(void) {
    TokenList *tokens = tokenizer_tokenize("|> sort");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NULL(list);
    token_list_free(tokens);
}

// --- Command list tests (;, &&, ||) ---

static void test_parser_semicolon_two_commands(void) {
    TokenList *tokens = tokenizer_tokenize("echo hello ; echo world");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NOT_NULL(list);
    ASSERT_INT_EQ(list->num_entries, 2);
    ASSERT_STR_EQ(list->entries[0].pipeline->commands[0]->argv[0], "echo");
    ASSERT_STR_EQ(list->entries[0].pipeline->commands[0]->argv[1], "hello");
    ASSERT_STR_EQ(list->entries[1].pipeline->commands[0]->argv[0], "echo");
    ASSERT_STR_EQ(list->entries[1].pipeline->commands[0]->argv[1], "world");
    ASSERT_INT_EQ(list->operators[0], LIST_SEMI);
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_and_operator(void) {
    TokenList *tokens = tokenizer_tokenize("true && echo ok");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NOT_NULL(list);
    ASSERT_INT_EQ(list->num_entries, 2);
    ASSERT_INT_EQ(list->operators[0], LIST_AND);
    ASSERT_STR_EQ(list->entries[0].pipeline->commands[0]->argv[0], "true");
    ASSERT_STR_EQ(list->entries[1].pipeline->commands[0]->argv[0], "echo");
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_or_operator(void) {
    TokenList *tokens = tokenizer_tokenize("false || echo fallback");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NOT_NULL(list);
    ASSERT_INT_EQ(list->num_entries, 2);
    ASSERT_INT_EQ(list->operators[0], LIST_OR);
    ASSERT_STR_EQ(list->entries[0].pipeline->commands[0]->argv[0], "false");
    ASSERT_STR_EQ(list->entries[1].pipeline->commands[0]->argv[0], "echo");
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_mixed_operators(void) {
    TokenList *tokens = tokenizer_tokenize("cmd1 && cmd2 || cmd3 ; cmd4");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NOT_NULL(list);
    ASSERT_INT_EQ(list->num_entries, 4);
    ASSERT_INT_EQ(list->operators[0], LIST_AND);
    ASSERT_INT_EQ(list->operators[1], LIST_OR);
    ASSERT_INT_EQ(list->operators[2], LIST_SEMI);
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_trailing_semicolon(void) {
    TokenList *tokens = tokenizer_tokenize("echo hello ;");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NOT_NULL(list);
    ASSERT_INT_EQ(list->num_entries, 1); // trailing ; doesn't create empty pipeline
    ASSERT_STR_EQ(list->entries[0].pipeline->commands[0]->argv[0], "echo");
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_semicolon_with_pipes(void) {
    TokenList *tokens = tokenizer_tokenize("ls | grep foo ; echo done");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NOT_NULL(list);
    ASSERT_INT_EQ(list->num_entries, 2);
    ASSERT_INT_EQ(list->entries[0].pipeline->num_commands, 2); // ls | grep foo
    ASSERT_INT_EQ(list->entries[1].pipeline->num_commands, 1); // echo done
    ASSERT_INT_EQ(list->operators[0], LIST_SEMI);
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_error_and_at_start(void) {
    TokenList *tokens = tokenizer_tokenize("&& echo foo");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NULL(list);
    token_list_free(tokens);
}

static void test_parser_error_and_no_rhs(void) {
    TokenList *tokens = tokenizer_tokenize("echo foo &&");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NULL(list);
    token_list_free(tokens);
}

static void test_parser_error_or_no_rhs(void) {
    TokenList *tokens = tokenizer_tokenize("echo foo ||");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NULL(list);
    token_list_free(tokens);
}

static void test_parser_three_semicolons(void) {
    TokenList *tokens = tokenizer_tokenize("a ; b ; c");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NOT_NULL(list);
    ASSERT_INT_EQ(list->num_entries, 3);
    ASSERT_STR_EQ(list->entries[0].pipeline->commands[0]->argv[0], "a");
    ASSERT_STR_EQ(list->entries[1].pipeline->commands[0]->argv[0], "b");
    ASSERT_STR_EQ(list->entries[2].pipeline->commands[0]->argv[0], "c");
    command_list_free(list);
    token_list_free(tokens);
}

// --- if/elif/else/fi tests ---

static void test_parser_if_basic(void) {
    TokenList *tokens = tokenizer_tokenize("if true; then echo yes; fi");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NOT_NULL(list);
    ASSERT_INT_EQ(list->num_entries, 1);
    ASSERT_INT_EQ(list->entries[0].type, NODE_IF);
    IfCommand *cmd = list->entries[0].if_cmd;
    ASSERT_NOT_NULL(cmd);
    ASSERT_INT_EQ(cmd->num_clauses, 1);
    // Condition: "true"
    ASSERT_INT_EQ(cmd->clauses[0].condition->num_entries, 1);
    ASSERT_STR_EQ(cmd->clauses[0].condition->entries[0].pipeline->commands[0]->argv[0], "true");
    // Body: "echo yes"
    ASSERT_INT_EQ(cmd->clauses[0].body->num_entries, 1);
    ASSERT_STR_EQ(cmd->clauses[0].body->entries[0].pipeline->commands[0]->argv[0], "echo");
    ASSERT_STR_EQ(cmd->clauses[0].body->entries[0].pipeline->commands[0]->argv[1], "yes");
    ASSERT_NULL(cmd->else_body);
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_if_else(void) {
    TokenList *tokens = tokenizer_tokenize("if false; then echo no; else echo yes; fi");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NOT_NULL(list);
    IfCommand *cmd = list->entries[0].if_cmd;
    ASSERT_INT_EQ(cmd->num_clauses, 1);
    ASSERT_NOT_NULL(cmd->else_body);
    ASSERT_INT_EQ(cmd->else_body->num_entries, 1);
    ASSERT_STR_EQ(cmd->else_body->entries[0].pipeline->commands[0]->argv[1], "yes");
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_if_elif_else(void) {
    TokenList *tokens = tokenizer_tokenize(
        "if false; then echo a; elif true; then echo b; else echo c; fi");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NOT_NULL(list);
    IfCommand *cmd = list->entries[0].if_cmd;
    ASSERT_INT_EQ(cmd->num_clauses, 2);
    ASSERT_NOT_NULL(cmd->else_body);
    // elif condition: "true"
    ASSERT_STR_EQ(cmd->clauses[1].condition->entries[0].pipeline->commands[0]->argv[0], "true");
    // elif body: "echo b"
    ASSERT_STR_EQ(cmd->clauses[1].body->entries[0].pipeline->commands[0]->argv[1], "b");
    // else body: "echo c"
    ASSERT_STR_EQ(cmd->else_body->entries[0].pipeline->commands[0]->argv[1], "c");
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_if_with_list(void) {
    // if-command followed by another command via ;
    TokenList *tokens = tokenizer_tokenize("if true; then echo yes; fi ; echo done");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NOT_NULL(list);
    ASSERT_INT_EQ(list->num_entries, 2);
    ASSERT_INT_EQ(list->entries[0].type, NODE_IF);
    ASSERT_INT_EQ(list->entries[1].type, NODE_PIPELINE);
    ASSERT_STR_EQ(list->entries[1].pipeline->commands[0]->argv[1], "done");
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_if_and_operator(void) {
    // if-command with && operator
    TokenList *tokens = tokenizer_tokenize("if true; then echo yes; fi && echo ok");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NOT_NULL(list);
    ASSERT_INT_EQ(list->num_entries, 2);
    ASSERT_INT_EQ(list->operators[0], LIST_AND);
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_if_compound_condition(void) {
    // Condition with && inside
    TokenList *tokens = tokenizer_tokenize("if true && false; then echo no; fi");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NOT_NULL(list);
    IfCommand *cmd = list->entries[0].if_cmd;
    ASSERT_INT_EQ(cmd->clauses[0].condition->num_entries, 2);
    ASSERT_INT_EQ(cmd->clauses[0].condition->operators[0], LIST_AND);
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_if_multi_body(void) {
    // Body with multiple commands
    TokenList *tokens = tokenizer_tokenize("if true; then echo a; echo b; fi");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NOT_NULL(list);
    IfCommand *cmd = list->entries[0].if_cmd;
    ASSERT_INT_EQ(cmd->clauses[0].body->num_entries, 2);
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_if_missing_then(void) {
    TokenList *tokens = tokenizer_tokenize("if true; echo yes; fi");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NULL(list);
    token_list_free(tokens);
}

static void test_parser_if_missing_fi(void) {
    TokenList *tokens = tokenizer_tokenize("if true; then echo yes");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NULL(list);
    token_list_free(tokens);
}

static void test_parser_nested_if(void) {
    TokenList *tokens = tokenizer_tokenize(
        "if true; then if false; then echo a; else echo b; fi; fi");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NOT_NULL(list);
    ASSERT_INT_EQ(list->entries[0].type, NODE_IF);
    IfCommand *outer = list->entries[0].if_cmd;
    // Body of outer if contains an inner if
    ASSERT_INT_EQ(outer->clauses[0].body->num_entries, 1);
    ASSERT_INT_EQ(outer->clauses[0].body->entries[0].type, NODE_IF);
    IfCommand *inner = outer->clauses[0].body->entries[0].if_cmd;
    ASSERT_INT_EQ(inner->num_clauses, 1);
    ASSERT_NOT_NULL(inner->else_body);
    command_list_free(list);
    token_list_free(tokens);
}

// --- for/in/do/done tests ---

static void test_parser_for_basic(void) {
    const char *input = "for x in a b c; do echo $x; done";
    TokenList *tokens = tokenizer_tokenize(input);
    CommandList *list = parser_parse(tokens, input);
    ASSERT_NOT_NULL(list);
    ASSERT_INT_EQ(list->num_entries, 1);
    ASSERT_INT_EQ(list->entries[0].type, NODE_FOR);
    ForCommand *cmd = list->entries[0].for_cmd;
    ASSERT_NOT_NULL(cmd);
    ASSERT_STR_EQ(cmd->var_name, "x");
    ASSERT_INT_EQ(cmd->num_words, 3);
    ASSERT_STR_EQ(cmd->words[0], "a");
    ASSERT_STR_EQ(cmd->words[1], "b");
    ASSERT_STR_EQ(cmd->words[2], "c");
    ASSERT_NOT_NULL(cmd->body_src);
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_for_multi_body(void) {
    const char *input = "for f in x y; do echo $f; echo ok; done";
    TokenList *tokens = tokenizer_tokenize(input);
    CommandList *list = parser_parse(tokens, input);
    ASSERT_NOT_NULL(list);
    ForCommand *cmd = list->entries[0].for_cmd;
    ASSERT_NOT_NULL(cmd->body_src);
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_for_with_list(void) {
    // for-loop followed by another command
    TokenList *tokens = tokenizer_tokenize("for x in a; do echo $x; done ; echo end");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NOT_NULL(list);
    ASSERT_INT_EQ(list->num_entries, 2);
    ASSERT_INT_EQ(list->entries[0].type, NODE_FOR);
    ASSERT_INT_EQ(list->entries[1].type, NODE_PIPELINE);
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_for_missing_in(void) {
    TokenList *tokens = tokenizer_tokenize("for x do echo $x; done");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NULL(list);
    token_list_free(tokens);
}

static void test_parser_for_missing_do(void) {
    TokenList *tokens = tokenizer_tokenize("for x in a b; echo $x; done");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NULL(list);
    token_list_free(tokens);
}

static void test_parser_for_missing_done(void) {
    TokenList *tokens = tokenizer_tokenize("for x in a; do echo $x");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NULL(list);
    token_list_free(tokens);
}

static void test_parser_for_empty_word_list(void) {
    TokenList *tokens = tokenizer_tokenize("for x in ; do echo $x; done");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NOT_NULL(list);
    ForCommand *cmd = list->entries[0].for_cmd;
    ASSERT_INT_EQ(cmd->num_words, 0);
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_for_nested_in_if(void) {
    TokenList *tokens = tokenizer_tokenize(
        "if true; then for x in a b; do echo $x; done; fi");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NOT_NULL(list);
    ASSERT_INT_EQ(list->entries[0].type, NODE_IF);
    IfCommand *ic = list->entries[0].if_cmd;
    ASSERT_INT_EQ(ic->clauses[0].body->entries[0].type, NODE_FOR);
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_while_basic(void) {
    const char *input = "while true; do echo hello; done";
    TokenList *tokens = tokenizer_tokenize(input);
    CommandList *list = parser_parse(tokens, input);
    ASSERT_NOT_NULL(list);
    ASSERT_INT_EQ(list->num_entries, 1);
    ASSERT_INT_EQ(list->entries[0].type, NODE_WHILE);
    WhileCommand *cmd = list->entries[0].while_cmd;
    ASSERT_NOT_NULL(cmd);
    ASSERT_INT_EQ(cmd->is_until, 0);
    ASSERT_NOT_NULL(cmd->cond_src);
    ASSERT_NOT_NULL(cmd->body_src);
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_until_basic(void) {
    const char *input = "until false; do echo waiting; done";
    TokenList *tokens = tokenizer_tokenize(input);
    CommandList *list = parser_parse(tokens, input);
    ASSERT_NOT_NULL(list);
    ASSERT_INT_EQ(list->entries[0].type, NODE_WHILE);
    WhileCommand *cmd = list->entries[0].while_cmd;
    ASSERT_NOT_NULL(cmd);
    ASSERT_INT_EQ(cmd->is_until, 1);
    ASSERT_NOT_NULL(cmd->cond_src);
    ASSERT_NOT_NULL(cmd->body_src);
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_while_multi_body(void) {
    const char *input = "while test $x -gt 0; do echo $x; echo ok; done";
    TokenList *tokens = tokenizer_tokenize(input);
    CommandList *list = parser_parse(tokens, input);
    ASSERT_NOT_NULL(list);
    WhileCommand *cmd = list->entries[0].while_cmd;
    ASSERT_NOT_NULL(cmd->body_src);
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_while_with_list(void) {
    TokenList *tokens = tokenizer_tokenize("while true; do echo a; done ; echo end");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NOT_NULL(list);
    ASSERT_INT_EQ(list->num_entries, 2);
    ASSERT_INT_EQ(list->entries[0].type, NODE_WHILE);
    ASSERT_INT_EQ(list->entries[1].type, NODE_PIPELINE);
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_while_missing_do(void) {
    TokenList *tokens = tokenizer_tokenize("while true; echo a; done");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NULL(list);
    token_list_free(tokens);
}

static void test_parser_while_missing_done(void) {
    TokenList *tokens = tokenizer_tokenize("while true; do echo a");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NULL(list);
    token_list_free(tokens);
}

static void test_parser_while_nested_in_if(void) {
    TokenList *tokens = tokenizer_tokenize(
        "if true; then while false; do echo a; done; fi");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NOT_NULL(list);
    ASSERT_INT_EQ(list->entries[0].type, NODE_IF);
    IfCommand *ic = list->entries[0].if_cmd;
    ASSERT_INT_EQ(ic->clauses[0].body->entries[0].type, NODE_WHILE);
    command_list_free(list);
    token_list_free(tokens);
}

static void test_parser_while_empty_condition(void) {
    // "while ; do echo a; done" — empty condition is a syntax error
    TokenList *tokens = tokenizer_tokenize("while ; do echo a; done");
    CommandList *list = parser_parse(tokens, NULL);
    ASSERT_NULL(list);
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

    // Command lists (;, &&, ||)
    test_parser_semicolon_two_commands();
    test_parser_and_operator();
    test_parser_or_operator();
    test_parser_mixed_operators();
    test_parser_trailing_semicolon();
    test_parser_semicolon_with_pipes();
    test_parser_error_and_at_start();
    test_parser_error_and_no_rhs();
    test_parser_error_or_no_rhs();
    test_parser_three_semicolons();

    // if/elif/else/fi
    test_parser_if_basic();
    test_parser_if_else();
    test_parser_if_elif_else();
    test_parser_if_with_list();
    test_parser_if_and_operator();
    test_parser_if_compound_condition();
    test_parser_if_multi_body();
    test_parser_if_missing_then();
    test_parser_if_missing_fi();
    test_parser_nested_if();

    // for/in/do/done
    test_parser_for_basic();
    test_parser_for_multi_body();
    test_parser_for_with_list();
    test_parser_for_missing_in();
    test_parser_for_missing_do();
    test_parser_for_missing_done();
    test_parser_for_empty_word_list();
    test_parser_for_nested_in_if();

    // while/until
    test_parser_while_basic();
    test_parser_until_basic();
    test_parser_while_multi_body();
    test_parser_while_with_list();
    test_parser_while_missing_do();
    test_parser_while_missing_done();
    test_parser_while_nested_in_if();
    test_parser_while_empty_condition();

    TEST_REPORT();
}
