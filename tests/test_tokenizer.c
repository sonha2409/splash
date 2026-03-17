#include "test.h"
#include "tokenizer.h"

static void test_tokenizer_empty_input(void) {
    TokenList *list = tokenizer_tokenize("");
    ASSERT_INT_EQ(list->count, 1);
    ASSERT_INT_EQ(list->tokens[0].type, TOKEN_EOF);
    token_list_free(list);
}

static void test_tokenizer_single_word(void) {
    TokenList *list = tokenizer_tokenize("hello");
    ASSERT_INT_EQ(list->count, 2);
    ASSERT_INT_EQ(list->tokens[0].type, TOKEN_WORD);
    ASSERT_STR_EQ(list->tokens[0].value, "hello");
    ASSERT_INT_EQ(list->tokens[0].pos, 0);
    ASSERT_INT_EQ(list->tokens[0].length, 5);
    ASSERT_INT_EQ(list->tokens[1].type, TOKEN_EOF);
    token_list_free(list);
}

static void test_tokenizer_multiple_words(void) {
    TokenList *list = tokenizer_tokenize("ls -al /tmp");
    ASSERT_INT_EQ(list->count, 4); // ls, -al, /tmp, EOF
    ASSERT_STR_EQ(list->tokens[0].value, "ls");
    ASSERT_STR_EQ(list->tokens[1].value, "-al");
    ASSERT_STR_EQ(list->tokens[2].value, "/tmp");
    ASSERT_INT_EQ(list->tokens[3].type, TOKEN_EOF);
    token_list_free(list);
}

static void test_tokenizer_pipe(void) {
    TokenList *list = tokenizer_tokenize("ls | grep foo");
    ASSERT_INT_EQ(list->count, 5); // ls, |, grep, foo, EOF
    ASSERT_INT_EQ(list->tokens[0].type, TOKEN_WORD);
    ASSERT_INT_EQ(list->tokens[1].type, TOKEN_PIPE);
    ASSERT_INT_EQ(list->tokens[2].type, TOKEN_WORD);
    ASSERT_INT_EQ(list->tokens[3].type, TOKEN_WORD);
    token_list_free(list);
}

static void test_tokenizer_structured_pipe(void) {
    TokenList *list = tokenizer_tokenize("ls |> where size > 100");
    ASSERT_INT_EQ(list->count, 7);
    ASSERT_INT_EQ(list->tokens[0].type, TOKEN_WORD);
    ASSERT_INT_EQ(list->tokens[1].type, TOKEN_PIPE_STRUCTURED);
    ASSERT_INT_EQ(list->tokens[2].type, TOKEN_WORD);
    ASSERT_INT_EQ(list->tokens[3].type, TOKEN_WORD);
    ASSERT_INT_EQ(list->tokens[4].type, TOKEN_REDIRECT_OUT);
    ASSERT_INT_EQ(list->tokens[5].type, TOKEN_WORD);
    token_list_free(list);
}

static void test_tokenizer_redirects(void) {
    TokenList *list = tokenizer_tokenize("cmd > out >> app < in");
    ASSERT_INT_EQ(list->count, 8);
    ASSERT_INT_EQ(list->tokens[0].type, TOKEN_WORD);    // cmd
    ASSERT_INT_EQ(list->tokens[1].type, TOKEN_REDIRECT_OUT); // >
    ASSERT_INT_EQ(list->tokens[2].type, TOKEN_WORD);    // out
    ASSERT_INT_EQ(list->tokens[3].type, TOKEN_REDIRECT_APPEND); // >>
    ASSERT_INT_EQ(list->tokens[4].type, TOKEN_WORD);    // app
    ASSERT_INT_EQ(list->tokens[5].type, TOKEN_REDIRECT_IN); // <
    ASSERT_INT_EQ(list->tokens[6].type, TOKEN_WORD);    // in
    token_list_free(list);
}

static void test_tokenizer_stderr_redirect(void) {
    TokenList *list = tokenizer_tokenize("cmd 2> err");
    ASSERT_INT_EQ(list->count, 4);
    ASSERT_INT_EQ(list->tokens[0].type, TOKEN_WORD);    // cmd
    ASSERT_INT_EQ(list->tokens[1].type, TOKEN_REDIRECT_ERR); // 2>
    ASSERT_INT_EQ(list->tokens[2].type, TOKEN_WORD);    // err
    token_list_free(list);
}

static void test_tokenizer_stderr_not_in_word(void) {
    // "file2>" should be word "file2" then ">"
    TokenList *list = tokenizer_tokenize("file2> out");
    ASSERT_INT_EQ(list->count, 4);
    ASSERT_INT_EQ(list->tokens[0].type, TOKEN_WORD);
    ASSERT_STR_EQ(list->tokens[0].value, "file2");
    ASSERT_INT_EQ(list->tokens[1].type, TOKEN_REDIRECT_OUT);
    token_list_free(list);
}

static void test_tokenizer_combined_redirects(void) {
    TokenList *list = tokenizer_tokenize("cmd >& out >>& app");
    ASSERT_INT_EQ(list->count, 6);
    ASSERT_INT_EQ(list->tokens[1].type, TOKEN_REDIRECT_OUT_ERR);
    ASSERT_INT_EQ(list->tokens[3].type, TOKEN_REDIRECT_APPEND_ERR);
    token_list_free(list);
}

static void test_tokenizer_and_or(void) {
    TokenList *list = tokenizer_tokenize("cmd1 && cmd2 || cmd3");
    ASSERT_INT_EQ(list->count, 6);
    ASSERT_INT_EQ(list->tokens[1].type, TOKEN_AND);
    ASSERT_INT_EQ(list->tokens[3].type, TOKEN_OR);
    token_list_free(list);
}

static void test_tokenizer_background(void) {
    TokenList *list = tokenizer_tokenize("sleep 10 &");
    ASSERT_INT_EQ(list->count, 4);
    ASSERT_INT_EQ(list->tokens[2].type, TOKEN_BACKGROUND);
    token_list_free(list);
}

static void test_tokenizer_semicolon(void) {
    TokenList *list = tokenizer_tokenize("cmd1 ; cmd2");
    ASSERT_INT_EQ(list->count, 4);
    ASSERT_INT_EQ(list->tokens[1].type, TOKEN_SEMICOLON);
    token_list_free(list);
}

static void test_tokenizer_parens(void) {
    TokenList *list = tokenizer_tokenize("(cmd1)");
    ASSERT_INT_EQ(list->count, 4);
    ASSERT_INT_EQ(list->tokens[0].type, TOKEN_LPAREN);
    ASSERT_INT_EQ(list->tokens[1].type, TOKEN_WORD);
    ASSERT_INT_EQ(list->tokens[2].type, TOKEN_RPAREN);
    token_list_free(list);
}

static void test_tokenizer_dollar_paren(void) {
    TokenList *list = tokenizer_tokenize("echo $(whoami)");
    ASSERT_INT_EQ(list->count, 5);
    ASSERT_INT_EQ(list->tokens[0].type, TOKEN_WORD);    // echo
    ASSERT_INT_EQ(list->tokens[1].type, TOKEN_DOLLAR_PAREN); // $(
    ASSERT_INT_EQ(list->tokens[2].type, TOKEN_WORD);    // whoami
    ASSERT_INT_EQ(list->tokens[3].type, TOKEN_RPAREN);  // )
    token_list_free(list);
}

static void test_tokenizer_process_sub(void) {
    TokenList *list = tokenizer_tokenize("diff <(cmd1) >(cmd2)");
    ASSERT_INT_EQ(list->count, 8);
    ASSERT_INT_EQ(list->tokens[1].type, TOKEN_PROCESS_SUB_IN);
    ASSERT_INT_EQ(list->tokens[4].type, TOKEN_PROCESS_SUB_OUT);
    token_list_free(list);
}

static void test_tokenizer_double_quotes(void) {
    TokenList *list = tokenizer_tokenize("echo \"hello world\"");
    ASSERT_INT_EQ(list->count, 3);
    ASSERT_STR_EQ(list->tokens[1].value, "hello world");
    token_list_free(list);
}

static void test_tokenizer_single_quotes(void) {
    TokenList *list = tokenizer_tokenize("echo 'hello $world'");
    ASSERT_INT_EQ(list->count, 3);
    ASSERT_STR_EQ(list->tokens[1].value, "hello $world");
    token_list_free(list);
}

static void test_tokenizer_backslash_escape(void) {
    TokenList *list = tokenizer_tokenize("echo hello\\ world");
    ASSERT_INT_EQ(list->count, 3);
    ASSERT_STR_EQ(list->tokens[1].value, "hello world");
    token_list_free(list);
}

static void test_tokenizer_unterminated_double_quote(void) {
    TokenList *list = tokenizer_tokenize("echo \"hello");
    // Should have WORD(echo), INCOMPLETE, EOF
    int has_incomplete = 0;
    for (int i = 0; i < list->count; i++) {
        if (list->tokens[i].type == TOKEN_INCOMPLETE) {
            has_incomplete = 1;
        }
    }
    ASSERT(has_incomplete);
    token_list_free(list);
}

static void test_tokenizer_unterminated_single_quote(void) {
    TokenList *list = tokenizer_tokenize("echo 'hello");
    int has_incomplete = 0;
    for (int i = 0; i < list->count; i++) {
        if (list->tokens[i].type == TOKEN_INCOMPLETE) {
            has_incomplete = 1;
        }
    }
    ASSERT(has_incomplete);
    token_list_free(list);
}

static void test_tokenizer_trailing_backslash(void) {
    TokenList *list = tokenizer_tokenize("echo \\");
    int has_incomplete = 0;
    for (int i = 0; i < list->count; i++) {
        if (list->tokens[i].type == TOKEN_INCOMPLETE) {
            has_incomplete = 1;
        }
    }
    ASSERT(has_incomplete);
    token_list_free(list);
}

static void test_tokenizer_triple_pipe(void) {
    // ||| should be || then |
    TokenList *list = tokenizer_tokenize("|||");
    ASSERT_INT_EQ(list->count, 3);
    ASSERT_INT_EQ(list->tokens[0].type, TOKEN_OR);
    ASSERT_INT_EQ(list->tokens[1].type, TOKEN_PIPE);
    token_list_free(list);
}

static void test_tokenizer_position_tracking(void) {
    TokenList *list = tokenizer_tokenize("ls | grep");
    // ls at 0, | at 3, grep at 5
    ASSERT_INT_EQ(list->tokens[0].pos, 0);
    ASSERT_INT_EQ(list->tokens[1].pos, 3);
    ASSERT_INT_EQ(list->tokens[2].pos, 5);
    token_list_free(list);
}

static void test_tokenizer_whitespace_variations(void) {
    TokenList *list = tokenizer_tokenize("  ls   -al   ");
    ASSERT_INT_EQ(list->count, 3); // ls, -al, EOF
    ASSERT_STR_EQ(list->tokens[0].value, "ls");
    ASSERT_STR_EQ(list->tokens[1].value, "-al");
    token_list_free(list);
}

static void test_tokenizer_adjacent_operators(void) {
    TokenList *list = tokenizer_tokenize(";;&");
    ASSERT_INT_EQ(list->count, 4); // ;, ;, &, EOF
    ASSERT_INT_EQ(list->tokens[0].type, TOKEN_SEMICOLON);
    ASSERT_INT_EQ(list->tokens[1].type, TOKEN_SEMICOLON);
    ASSERT_INT_EQ(list->tokens[2].type, TOKEN_BACKGROUND);
    token_list_free(list);
}

int main(void) {
    printf("test_tokenizer\n");

    test_tokenizer_empty_input();
    test_tokenizer_single_word();
    test_tokenizer_multiple_words();
    test_tokenizer_pipe();
    test_tokenizer_structured_pipe();
    test_tokenizer_redirects();
    test_tokenizer_stderr_redirect();
    test_tokenizer_stderr_not_in_word();
    test_tokenizer_combined_redirects();
    test_tokenizer_and_or();
    test_tokenizer_background();
    test_tokenizer_semicolon();
    test_tokenizer_parens();
    test_tokenizer_dollar_paren();
    test_tokenizer_process_sub();
    test_tokenizer_double_quotes();
    test_tokenizer_single_quotes();
    test_tokenizer_backslash_escape();
    test_tokenizer_unterminated_double_quote();
    test_tokenizer_unterminated_single_quote();
    test_tokenizer_trailing_backslash();
    test_tokenizer_triple_pipe();
    test_tokenizer_position_tracking();
    test_tokenizer_whitespace_variations();
    test_tokenizer_adjacent_operators();

    TEST_REPORT();
}
