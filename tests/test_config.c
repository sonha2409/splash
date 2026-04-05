#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"
#include "test.h"


// Helper: remove directory if it exists (non-recursive, empty dirs only)
static void rmdir_if_exists(const char *path) {
    rmdir(path);
}

// Test: config_init creates directory under $HOME/.config/splash/
static void test_config_init_creates_dir(void) {
    config_reset();
    // Use a temp directory as HOME
    char tmpdir[] = "/tmp/splash_test_config_XXXXXX";
    char *dir = mkdtemp(tmpdir);
    ASSERT_NOT_NULL(dir);

    // Clear XDG, set HOME to our temp dir
    unsetenv("XDG_CONFIG_HOME");
    setenv("HOME", dir, 1);

    config_init();
    const char *result = config_get_dir();
    ASSERT_NOT_NULL(result);

    // Verify directory was created
    struct stat st;
    ASSERT(stat(result, &st) == 0);
    ASSERT(S_ISDIR(st.st_mode));

    // Verify path ends with .config/splash
    char expected[1024];
    snprintf(expected, sizeof(expected), "%s/.config/splash", dir);
    ASSERT_STR_EQ(result, expected);

    // Cleanup
    rmdir_if_exists(expected);
    char parent[1024];
    snprintf(parent, sizeof(parent), "%s/.config", dir);
    rmdir_if_exists(parent);
    rmdir_if_exists(dir);
}

// Test: config_init respects $XDG_CONFIG_HOME
static void test_config_init_xdg(void) {
    config_reset();
    char tmpdir[] = "/tmp/splash_test_xdg_XXXXXX";
    char *dir = mkdtemp(tmpdir);
    ASSERT_NOT_NULL(dir);

    setenv("XDG_CONFIG_HOME", dir, 1);

    config_init();
    const char *result = config_get_dir();
    ASSERT_NOT_NULL(result);

    char expected[1024];
    snprintf(expected, sizeof(expected), "%s/splash", dir);
    ASSERT_STR_EQ(result, expected);

    struct stat st;
    ASSERT(stat(result, &st) == 0);
    ASSERT(S_ISDIR(st.st_mode));

    // Cleanup
    rmdir_if_exists(expected);
    rmdir_if_exists(dir);
    unsetenv("XDG_CONFIG_HOME");
}

// Test: config_init with empty XDG_CONFIG_HOME falls back to HOME
static void test_config_init_xdg_empty(void) {
    config_reset();
    char tmpdir[] = "/tmp/splash_test_xdgempty_XXXXXX";
    char *dir = mkdtemp(tmpdir);
    ASSERT_NOT_NULL(dir);

    setenv("XDG_CONFIG_HOME", "", 1);
    setenv("HOME", dir, 1);

    config_init();
    const char *result = config_get_dir();
    ASSERT_NOT_NULL(result);

    char expected[1024];
    snprintf(expected, sizeof(expected), "%s/.config/splash", dir);
    ASSERT_STR_EQ(result, expected);

    // Cleanup
    rmdir_if_exists(expected);
    char parent[1024];
    snprintf(parent, sizeof(parent), "%s/.config", dir);
    rmdir_if_exists(parent);
    rmdir_if_exists(dir);
    unsetenv("XDG_CONFIG_HOME");
}

// Test: config_get_dir returns NULL if neither HOME nor XDG set
static void test_config_init_no_home(void) {
    config_reset();
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("HOME");

    config_init();
    const char *result = config_get_dir();
    ASSERT_NULL(result);
}

// Test: config_init is idempotent when directory already exists
static void test_config_init_existing_dir(void) {
    config_reset();
    char tmpdir[] = "/tmp/splash_test_existing_XXXXXX";
    char *dir = mkdtemp(tmpdir);
    ASSERT_NOT_NULL(dir);

    // Pre-create the directory structure
    char config_path[1024];
    snprintf(config_path, sizeof(config_path), "%s/.config", dir);
    mkdir(config_path, 0755);
    char splash_path[1024];
    snprintf(splash_path, sizeof(splash_path), "%s/.config/splash", dir);
    mkdir(splash_path, 0755);

    unsetenv("XDG_CONFIG_HOME");
    setenv("HOME", dir, 1);

    config_init();
    const char *result = config_get_dir();
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, splash_path);

    // Cleanup
    rmdir_if_exists(splash_path);
    rmdir_if_exists(config_path);
    rmdir_if_exists(dir);
}

// Helper: write a string to a file
static void write_file(const char *path, const char *content) {
    FILE *fp = fopen(path, "w");
    if (fp) {
        fputs(content, fp);
        fclose(fp);
    }
}


// --- TOML parsing tests ---

static void test_config_toml_basic(void) {
    config_reset();
    char tmpfile[] = "/tmp/splash_test_toml_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT(fd >= 0);
    close(fd);

    write_file(tmpfile,
        "# A comment\n"
        "[prompt]\n"
        "format = \"\\u@\\h \\w> \"\n"
        "\n"
        "[history]\n"
        "max_size = 5000\n"
        "save = true\n"
    );

    config_load_from(tmpfile);

    ASSERT_STR_EQ(config_get_string("prompt.format"), "\\u@\\h \\w> ");
    ASSERT_INT_EQ(config_get_int("history.max_size", 0), 5000);
    ASSERT_INT_EQ(config_get_bool("history.save", 0), 1);

    unlink(tmpfile);
}

static void test_config_toml_single_quotes(void) {
    config_reset();
    char tmpfile[] = "/tmp/splash_test_toml_sq_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT(fd >= 0);
    close(fd);

    write_file(tmpfile,
        "[prompt]\n"
        "format = 'hello world'\n"
    );

    config_load_from(tmpfile);
    ASSERT_STR_EQ(config_get_string("prompt.format"), "hello world");

    unlink(tmpfile);
}

static void test_config_toml_bare_values(void) {
    config_reset();
    char tmpfile[] = "/tmp/splash_test_toml_bare_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT(fd >= 0);
    close(fd);

    write_file(tmpfile,
        "top_level = 42\n"
        "[section]\n"
        "flag = false\n"
        "name = hello\n"
    );

    config_load_from(tmpfile);
    ASSERT_INT_EQ(config_get_int("top_level", 0), 42);
    ASSERT_INT_EQ(config_get_bool("section.flag", 1), 0);
    ASSERT_STR_EQ(config_get_string("section.name"), "hello");

    unlink(tmpfile);
}

static void test_config_toml_inline_comments(void) {
    config_reset();
    char tmpfile[] = "/tmp/splash_test_toml_ic_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT(fd >= 0);
    close(fd);

    write_file(tmpfile,
        "[test]\n"
        "value = 100 # this is a comment\n"
    );

    config_load_from(tmpfile);
    ASSERT_INT_EQ(config_get_int("test.value", 0), 100);

    unlink(tmpfile);
}

static void test_config_toml_escape_sequences(void) {
    config_reset();
    char tmpfile[] = "/tmp/splash_test_toml_esc_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT(fd >= 0);
    close(fd);

    write_file(tmpfile,
        "[test]\n"
        "tab = \"hello\\tworld\"\n"
        "newline = \"line1\\nline2\"\n"
        "quote = \"say \\\"hi\\\"\"\n"
    );

    config_load_from(tmpfile);
    ASSERT_STR_EQ(config_get_string("test.tab"), "hello\tworld");
    ASSERT_STR_EQ(config_get_string("test.newline"), "line1\nline2");
    ASSERT_STR_EQ(config_get_string("test.quote"), "say \"hi\"");

    unlink(tmpfile);
}

static void test_config_toml_duplicate_keys(void) {
    config_reset();
    char tmpfile[] = "/tmp/splash_test_toml_dup_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT(fd >= 0);
    close(fd);

    write_file(tmpfile,
        "[test]\n"
        "val = \"first\"\n"
        "val = \"second\"\n"
    );

    config_load_from(tmpfile);
    ASSERT_STR_EQ(config_get_string("test.val"), "second");

    unlink(tmpfile);
}

static void test_config_get_missing_key(void) {
    config_reset();
    ASSERT_NULL(config_get_string("nonexistent.key"));
    ASSERT_INT_EQ(config_get_int("nonexistent.key", 42), 42);
    ASSERT_INT_EQ(config_get_bool("nonexistent.key", 1), 1);
}

static void test_config_get_int_invalid(void) {
    config_reset();
    char tmpfile[] = "/tmp/splash_test_toml_inv_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT(fd >= 0);
    close(fd);

    write_file(tmpfile,
        "[test]\n"
        "not_a_number = \"hello\"\n"
    );

    config_load_from(tmpfile);
    ASSERT_INT_EQ(config_get_int("test.not_a_number", 99), 99);

    unlink(tmpfile);
}

static void test_config_toml_bool_case_insensitive(void) {
    config_reset();
    char tmpfile[] = "/tmp/splash_test_toml_bool_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT(fd >= 0);
    close(fd);

    write_file(tmpfile,
        "[test]\n"
        "a = True\n"
        "b = FALSE\n"
        "c = \"not_bool\"\n"
    );

    config_load_from(tmpfile);
    ASSERT_INT_EQ(config_get_bool("test.a", 0), 1);
    ASSERT_INT_EQ(config_get_bool("test.b", 1), 0);
    ASSERT_INT_EQ(config_get_bool("test.c", 0), 0); // default when not a bool

    unlink(tmpfile);
}

// --- Prompt expansion tests ---

static void test_prompt_expand_literal(void) {
    config_reset();
    char *result = config_expand_prompt("hello> ");
    ASSERT_STR_EQ(result, "hello> ");
    free(result);
}

static void test_prompt_expand_user(void) {
    config_reset();
    setenv("USER", "testuser", 1);
    char *result = config_expand_prompt("\\u$ ");
    ASSERT_STR_EQ(result, "testuser$ ");
    free(result);
}

static void test_prompt_expand_cwd(void) {
    config_reset();
    // \W should give basename of cwd
    char *result = config_expand_prompt("\\W> ");
    ASSERT_NOT_NULL(result);
    // Should not be empty
    ASSERT(strlen(result) > 2);
    free(result);
}

static void test_prompt_expand_dollar(void) {
    config_reset();
    char *result = config_expand_prompt("\\$ ");
    // We're not root, so should be "$ "
    ASSERT_STR_EQ(result, "$ ");
    free(result);
}

static void test_prompt_expand_backslash(void) {
    config_reset();
    char *result = config_expand_prompt("a\\\\b");
    ASSERT_STR_EQ(result, "a\\b");
    free(result);
}

static void test_prompt_expand_escape(void) {
    config_reset();
    char *result = config_expand_prompt("\\e[32m>");
    // \e should produce ESC (0x1b)
    ASSERT(result[0] == '\033');
    ASSERT_STR_EQ(result + 1, "[32m>");
    free(result);
}

static void test_prompt_expand_git(void) {
    config_reset();
    // We're in a git repo, so \g should return something
    char *result = config_expand_prompt("(\\g) ");
    ASSERT_NOT_NULL(result);
    // Should contain parens at minimum
    ASSERT(result[0] == '(');
    free(result);
}

static void test_prompt_expand_home_tilde(void) {
    config_reset();
    // cd to HOME, then \w should start with ~
    const char *home = getenv("HOME");
    if (home) {
        char orig_cwd[1024];
        if (getcwd(orig_cwd, sizeof(orig_cwd))) {
            if (chdir(home) == 0) {
                char *result = config_expand_prompt("\\w ");
                ASSERT(result[0] == '~');
                free(result);
                chdir(orig_cwd);
            }
        }
    }
}

static void test_config_build_prompt_default(void) {
    config_reset();
    unsetenv("PROMPT");
    char *result = config_build_prompt();
    ASSERT_STR_EQ(result, "splash> ");
    free(result);
}

static void test_config_build_prompt_env(void) {
    config_reset();
    setenv("PROMPT", "\\u@\\h \\$ ", 1);
    char *result = config_build_prompt();
    ASSERT_NOT_NULL(result);
    // Should start with username
    const char *user = getenv("USER");
    if (user) {
        ASSERT(strncmp(result, user, strlen(user)) == 0);
    }
    free(result);
    unsetenv("PROMPT");
}

static void test_config_build_prompt_config(void) {
    config_reset();
    unsetenv("PROMPT");
    // Load a config with prompt.format
    char tmpfile[] = "/tmp/splash_test_prompt_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT(fd >= 0);
    close(fd);

    write_file(tmpfile,
        "[prompt]\n"
        "format = \"custom> \"\n"
    );
    config_load_from(tmpfile);

    char *result = config_build_prompt();
    ASSERT_STR_EQ(result, "custom> ");
    free(result);
    unlink(tmpfile);
}

int main(void) {
    printf("test_config:\n");

    // Save original env
    const char *orig_home = getenv("HOME");
    char *saved_home = orig_home ? strdup(orig_home) : NULL;
    const char *orig_xdg = getenv("XDG_CONFIG_HOME");
    char *saved_xdg = orig_xdg ? strdup(orig_xdg) : NULL;

    // 9.1 tests
    test_config_init_creates_dir();
    test_config_init_xdg();
    test_config_init_xdg_empty();
    test_config_init_no_home();
    test_config_init_existing_dir();

    // 9.2 tests
    test_config_toml_basic();
    test_config_toml_single_quotes();
    test_config_toml_bare_values();
    test_config_toml_inline_comments();
    test_config_toml_escape_sequences();
    test_config_toml_duplicate_keys();
    test_config_get_missing_key();
    test_config_get_int_invalid();
    test_config_toml_bool_case_insensitive();

    // 9.5 tests
    test_prompt_expand_literal();
    test_prompt_expand_user();
    test_prompt_expand_cwd();
    test_prompt_expand_dollar();
    test_prompt_expand_backslash();
    test_prompt_expand_escape();
    test_prompt_expand_git();
    test_prompt_expand_home_tilde();
    test_config_build_prompt_default();
    test_config_build_prompt_env();
    test_config_build_prompt_config();

    // Restore original env
    if (saved_home) {
        setenv("HOME", saved_home, 1);
        free(saved_home);
    }
    if (saved_xdg) {
        setenv("XDG_CONFIG_HOME", saved_xdg, 1);
        free(saved_xdg);
    }

    TEST_REPORT();
}
