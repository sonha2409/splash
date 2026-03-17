#include "test.h"
#include "highlight.h"

static void test_highlight_empty_input(void) {
    HighlightType *colors = highlight_line("", 0);
    ASSERT_NOT_NULL(colors);
    free(colors);
}

static void test_highlight_valid_command(void) {
    // "ls" should be highlighted as a valid command (green)
    const char *input = "ls";
    HighlightType *colors = highlight_line(input, strlen(input));
    ASSERT_NOT_NULL(colors);
    ASSERT_INT_EQ(colors[0], HL_COMMAND);
    ASSERT_INT_EQ(colors[1], HL_COMMAND);
    free(colors);
}

static void test_highlight_invalid_command(void) {
    // "xyznotfound" should be highlighted as error (red)
    const char *input = "xyznotfound";
    HighlightType *colors = highlight_line(input, strlen(input));
    ASSERT_NOT_NULL(colors);
    for (size_t i = 0; i < strlen(input); i++) {
        ASSERT_INT_EQ(colors[i], HL_ERROR);
    }
    free(colors);
}

static void test_highlight_command_with_args(void) {
    // "ls -la /tmp" — "ls" green, "-la" and "/tmp" default
    const char *input = "ls -la /tmp";
    HighlightType *colors = highlight_line(input, strlen(input));
    ASSERT_NOT_NULL(colors);
    // "ls" = command
    ASSERT_INT_EQ(colors[0], HL_COMMAND);
    ASSERT_INT_EQ(colors[1], HL_COMMAND);
    // space = default
    ASSERT_INT_EQ(colors[2], HL_DEFAULT);
    // "-la" = default (argument)
    ASSERT_INT_EQ(colors[3], HL_DEFAULT);
    ASSERT_INT_EQ(colors[4], HL_DEFAULT);
    ASSERT_INT_EQ(colors[5], HL_DEFAULT);
    free(colors);
}

static void test_highlight_pipe_resets_command_position(void) {
    // "ls | grep" — both "ls" and "grep" should be commands
    const char *input = "ls | grep";
    HighlightType *colors = highlight_line(input, strlen(input));
    ASSERT_NOT_NULL(colors);
    // "ls" = command
    ASSERT_INT_EQ(colors[0], HL_COMMAND);
    ASSERT_INT_EQ(colors[1], HL_COMMAND);
    // " " = default
    ASSERT_INT_EQ(colors[2], HL_DEFAULT);
    // "|" = operator
    ASSERT_INT_EQ(colors[3], HL_OPERATOR);
    // " " = default
    ASSERT_INT_EQ(colors[4], HL_DEFAULT);
    // "grep" = command
    ASSERT_INT_EQ(colors[5], HL_COMMAND);
    ASSERT_INT_EQ(colors[6], HL_COMMAND);
    ASSERT_INT_EQ(colors[7], HL_COMMAND);
    ASSERT_INT_EQ(colors[8], HL_COMMAND);
    free(colors);
}

static void test_highlight_single_quoted_string(void) {
    // "echo 'hello world'" — string in yellow
    const char *input = "echo 'hello world'";
    HighlightType *colors = highlight_line(input, strlen(input));
    ASSERT_NOT_NULL(colors);
    // "echo" = command (position 0-3)
    ASSERT_INT_EQ(colors[0], HL_COMMAND);
    // 'hello world' (position 5-17) = string
    for (size_t i = 5; i <= 17; i++) {
        ASSERT_INT_EQ(colors[i], HL_STRING);
    }
    free(colors);
}

static void test_highlight_double_quoted_string(void) {
    const char *input = "echo \"hello\"";
    HighlightType *colors = highlight_line(input, strlen(input));
    ASSERT_NOT_NULL(colors);
    // "hello" (position 5-11) = string
    ASSERT_INT_EQ(colors[5], HL_STRING);  // opening "
    ASSERT_INT_EQ(colors[6], HL_STRING);  // h
    ASSERT_INT_EQ(colors[11], HL_STRING); // closing "
    free(colors);
}

static void test_highlight_variable(void) {
    // "echo $HOME" — $HOME in magenta
    const char *input = "echo $HOME";
    HighlightType *colors = highlight_line(input, strlen(input));
    ASSERT_NOT_NULL(colors);
    // $HOME = positions 5-9
    for (size_t i = 5; i <= 9; i++) {
        ASSERT_INT_EQ(colors[i], HL_VARIABLE);
    }
    free(colors);
}

static void test_highlight_variable_in_double_quotes(void) {
    // 'echo "$HOME"' — $HOME still magenta inside double quotes
    const char *input = "echo \"$HOME\"";
    HighlightType *colors = highlight_line(input, strlen(input));
    ASSERT_NOT_NULL(colors);
    // opening " = string
    ASSERT_INT_EQ(colors[5], HL_STRING);
    // $HOME = variable (positions 6-10)
    for (size_t i = 6; i <= 10; i++) {
        ASSERT_INT_EQ(colors[i], HL_VARIABLE);
    }
    // closing " = string
    ASSERT_INT_EQ(colors[11], HL_STRING);
    free(colors);
}

static void test_highlight_comment(void) {
    // "# this is a comment"
    const char *input = "# this is a comment";
    HighlightType *colors = highlight_line(input, strlen(input));
    ASSERT_NOT_NULL(colors);
    for (size_t i = 0; i < strlen(input); i++) {
        ASSERT_INT_EQ(colors[i], HL_COMMENT);
    }
    free(colors);
}

static void test_highlight_redirect(void) {
    // "ls > out.txt" — > is operator, out.txt is default (not command)
    const char *input = "ls > out.txt";
    HighlightType *colors = highlight_line(input, strlen(input));
    ASSERT_NOT_NULL(colors);
    ASSERT_INT_EQ(colors[0], HL_COMMAND); // l
    ASSERT_INT_EQ(colors[1], HL_COMMAND); // s
    ASSERT_INT_EQ(colors[3], HL_OPERATOR); // >
    // out.txt should be default (not command position after redirect)
    ASSERT_INT_EQ(colors[5], HL_DEFAULT); // o
    free(colors);
}

static void test_highlight_and_operator(void) {
    // "ls && echo" — both commands, && is operator
    const char *input = "ls && echo";
    HighlightType *colors = highlight_line(input, strlen(input));
    ASSERT_NOT_NULL(colors);
    ASSERT_INT_EQ(colors[0], HL_COMMAND); // l
    ASSERT_INT_EQ(colors[1], HL_COMMAND); // s
    ASSERT_INT_EQ(colors[3], HL_OPERATOR); // first &
    ASSERT_INT_EQ(colors[4], HL_OPERATOR); // second &
    ASSERT_INT_EQ(colors[6], HL_COMMAND); // e
    free(colors);
}

static void test_highlight_semicolon(void) {
    // "ls; echo" — both commands
    const char *input = "ls; echo";
    HighlightType *colors = highlight_line(input, strlen(input));
    ASSERT_NOT_NULL(colors);
    ASSERT_INT_EQ(colors[0], HL_COMMAND); // l
    ASSERT_INT_EQ(colors[1], HL_COMMAND); // s
    ASSERT_INT_EQ(colors[2], HL_OPERATOR); // ;
    // "echo" starts at position 4
    ASSERT_INT_EQ(colors[4], HL_COMMAND); // e
    free(colors);
}

static void test_highlight_builtin_command(void) {
    // "cd /tmp" — "cd" is a builtin, should be green
    const char *input = "cd /tmp";
    HighlightType *colors = highlight_line(input, strlen(input));
    ASSERT_NOT_NULL(colors);
    ASSERT_INT_EQ(colors[0], HL_COMMAND);
    ASSERT_INT_EQ(colors[1], HL_COMMAND);
    free(colors);
}

static void test_highlight_command_substitution(void) {
    // "echo $(ls)" — $() in magenta
    const char *input = "echo $(ls)";
    HighlightType *colors = highlight_line(input, strlen(input));
    ASSERT_NOT_NULL(colors);
    // $(ls) = positions 5-9 = variable
    for (size_t i = 5; i <= 9; i++) {
        ASSERT_INT_EQ(colors[i], HL_VARIABLE);
    }
    free(colors);
}

static void test_highlight_braced_variable(void) {
    // "echo ${HOME}" — ${HOME} in magenta
    const char *input = "echo ${HOME}";
    HighlightType *colors = highlight_line(input, strlen(input));
    ASSERT_NOT_NULL(colors);
    for (size_t i = 5; i <= 11; i++) {
        ASSERT_INT_EQ(colors[i], HL_VARIABLE);
    }
    free(colors);
}

static void test_highlight_or_operator(void) {
    // "false || echo" — both commands, || is operator
    const char *input = "false || echo";
    HighlightType *colors = highlight_line(input, strlen(input));
    ASSERT_NOT_NULL(colors);
    // "false" starts at 0 — it's a valid command
    ASSERT_INT_EQ(colors[0], HL_COMMAND);
    // "||" at positions 6-7
    ASSERT_INT_EQ(colors[6], HL_OPERATOR);
    ASSERT_INT_EQ(colors[7], HL_OPERATOR);
    // "echo" at position 9
    ASSERT_INT_EQ(colors[9], HL_COMMAND);
    free(colors);
}

int main(void) {
    printf("test_highlight\n");

    test_highlight_empty_input();
    test_highlight_valid_command();
    test_highlight_invalid_command();
    test_highlight_command_with_args();
    test_highlight_pipe_resets_command_position();
    test_highlight_single_quoted_string();
    test_highlight_double_quoted_string();
    test_highlight_variable();
    test_highlight_variable_in_double_quotes();
    test_highlight_comment();
    test_highlight_redirect();
    test_highlight_and_operator();
    test_highlight_semicolon();
    test_highlight_builtin_command();
    test_highlight_command_substitution();
    test_highlight_braced_variable();
    test_highlight_or_operator();

    TEST_REPORT();
}
