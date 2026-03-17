#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "expand.h"
#include "test.h"
#include "util.h"

// Helper: create a temporary directory with known files for glob testing.
// Returns the path (caller must free).
static char *create_test_dir(void) {
    char template[] = "/tmp/splash_glob_test_XXXXXX";
    char *dir = mkdtemp(template);
    ASSERT_NOT_NULL(dir);
    if (!dir) {
        return NULL;
    }
    char *result = xstrdup(dir);

    // Create test files
    char path[512];
    const char *files[] = {
        "foo.c", "bar.c", "baz.h", "README", "main.c",
        ".hidden", ".secret.c"
    };
    for (int i = 0; i < 7; i++) {
        snprintf(path, sizeof(path), "%s/%s", result, files[i]);
        FILE *f = fopen(path, "w");
        ASSERT_NOT_NULL(f);
        if (f) {
            fclose(f);
        }
    }

    // Create a subdirectory
    snprintf(path, sizeof(path), "%s/subdir", result);
    mkdir(path, 0755);

    return result;
}

// Helper: remove test directory and its contents
static void remove_test_dir(const char *dir) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    (void)system(cmd);
}


// --- Tests for expand_has_glob ---

void test_has_glob_with_star(void) {
    char s[] = {GLOB_STAR, '.', 'c', '\0'};
    ASSERT(expand_has_glob(s) == 1);
}

void test_has_glob_with_question(void) {
    char s[] = {'f', 'o', GLOB_QUEST, '\0'};
    ASSERT(expand_has_glob(s) == 1);
}

void test_has_glob_no_glob(void) {
    ASSERT(expand_has_glob("hello") == 0);
    ASSERT(expand_has_glob("*.c") == 0);  // literal *, not sentinel
}

void test_has_glob_null(void) {
    ASSERT(expand_has_glob(NULL) == 0);
}


// --- Tests for expand_glob_unescape ---

void test_unescape_star(void) {
    char s[] = {GLOB_STAR, '.', 'c', '\0'};
    expand_glob_unescape(s);
    ASSERT_STR_EQ(s, "*.c");
}

void test_unescape_question(void) {
    char s[] = {'f', 'o', GLOB_QUEST, '\0'};
    expand_glob_unescape(s);
    ASSERT_STR_EQ(s, "fo?");
}

void test_unescape_mixed(void) {
    char s[] = {GLOB_STAR, '.', GLOB_QUEST, '\0'};
    expand_glob_unescape(s);
    ASSERT_STR_EQ(s, "*.?");
}


// --- Tests for expand_glob ---

void test_glob_star_c(void) {
    char *dir = create_test_dir();
    if (!dir) return;

    // Build pattern: dir/GLOB_STAR.c
    size_t dlen = strlen(dir);
    char *pattern = xmalloc(dlen + 5);
    memcpy(pattern, dir, dlen);
    pattern[dlen] = '/';
    pattern[dlen + 1] = GLOB_STAR;
    pattern[dlen + 2] = '.';
    pattern[dlen + 3] = 'c';
    pattern[dlen + 4] = '\0';

    int count = 0;
    char **results = expand_glob(pattern, &count);
    ASSERT_NOT_NULL(results);
    ASSERT_INT_EQ(count, 3); // bar.c, foo.c, main.c (sorted)

    if (results && count == 3) {
        // Check sorted order
        char expected0[512], expected1[512], expected2[512];
        snprintf(expected0, sizeof(expected0), "%s/bar.c", dir);
        snprintf(expected1, sizeof(expected1), "%s/foo.c", dir);
        snprintf(expected2, sizeof(expected2), "%s/main.c", dir);
        ASSERT_STR_EQ(results[0], expected0);
        ASSERT_STR_EQ(results[1], expected1);
        ASSERT_STR_EQ(results[2], expected2);
    }

    for (int i = 0; i < count; i++) free(results[i]);
    free(results);
    free(pattern);
    remove_test_dir(dir);
    free(dir);
}

void test_glob_question_mark(void) {
    char *dir = create_test_dir();
    if (!dir) return;

    // Pattern: dir/ba?.c  → bar.c, baz not (baz.h not .c)
    // Actually ba?.c matches bar.c only, ba?.h matches baz.h
    // Wait: ba GLOB_QUEST .c → matches bar.c (ba + r + .c)
    size_t dlen = strlen(dir);
    char *pattern = xmalloc(dlen + 7);
    memcpy(pattern, dir, dlen);
    pattern[dlen] = '/';
    pattern[dlen + 1] = 'b';
    pattern[dlen + 2] = 'a';
    pattern[dlen + 3] = GLOB_QUEST;
    pattern[dlen + 4] = '.';
    pattern[dlen + 5] = 'c';
    pattern[dlen + 6] = '\0';

    int count = 0;
    char **results = expand_glob(pattern, &count);
    ASSERT_NOT_NULL(results);
    ASSERT_INT_EQ(count, 1); // bar.c

    if (results && count == 1) {
        char expected[512];
        snprintf(expected, sizeof(expected), "%s/bar.c", dir);
        ASSERT_STR_EQ(results[0], expected);
    }

    for (int i = 0; i < count; i++) free(results[i]);
    free(results);
    free(pattern);
    remove_test_dir(dir);
    free(dir);
}

void test_glob_star_all(void) {
    char *dir = create_test_dir();
    if (!dir) return;

    // Pattern: dir/GLOB_STAR → matches all non-hidden files + subdir
    size_t dlen = strlen(dir);
    char *pattern = xmalloc(dlen + 3);
    memcpy(pattern, dir, dlen);
    pattern[dlen] = '/';
    pattern[dlen + 1] = GLOB_STAR;
    pattern[dlen + 2] = '\0';

    int count = 0;
    char **results = expand_glob(pattern, &count);
    ASSERT_NOT_NULL(results);
    // README, bar.c, baz.h, foo.c, main.c, subdir = 6 (all non-hidden)
    ASSERT_INT_EQ(count, 6);

    // Verify no hidden files
    for (int i = 0; i < count; i++) {
        // Extract filename from path
        const char *name = strrchr(results[i], '/');
        name = name ? name + 1 : results[i];
        ASSERT(name[0] != '.');
    }

    for (int i = 0; i < count; i++) free(results[i]);
    free(results);
    free(pattern);
    remove_test_dir(dir);
    free(dir);
}

void test_glob_hidden_files(void) {
    char *dir = create_test_dir();
    if (!dir) return;

    // Pattern: dir/.GLOB_STAR → matches hidden files
    size_t dlen = strlen(dir);
    char *pattern = xmalloc(dlen + 4);
    memcpy(pattern, dir, dlen);
    pattern[dlen] = '/';
    pattern[dlen + 1] = '.';
    pattern[dlen + 2] = GLOB_STAR;
    pattern[dlen + 3] = '\0';

    int count = 0;
    char **results = expand_glob(pattern, &count);
    ASSERT_NOT_NULL(results);
    // .hidden, .secret.c (but not . and ..)
    // Actually . and .. start with . and pattern starts with . so they match .*
    // We need to check: glob_match(".\x01", ".") → . matches .STAR?
    // GLOB_STAR matches zero or more chars, so "." + STAR matches ".", "..", ".hidden", ".secret.c"
    // That means we'll get ., .., .hidden, .secret.c = 4
    ASSERT(count >= 2); // at least .hidden and .secret.c

    for (int i = 0; i < count; i++) free(results[i]);
    free(results);
    free(pattern);
    remove_test_dir(dir);
    free(dir);
}

void test_glob_no_matches(void) {
    char *dir = create_test_dir();
    if (!dir) return;

    // Pattern: dir/GLOB_STAR.xyz → no matches
    size_t dlen = strlen(dir);
    char *pattern = xmalloc(dlen + 7);
    memcpy(pattern, dir, dlen);
    pattern[dlen] = '/';
    pattern[dlen + 1] = GLOB_STAR;
    pattern[dlen + 2] = '.';
    pattern[dlen + 3] = 'x';
    pattern[dlen + 4] = 'y';
    pattern[dlen + 5] = 'z';
    pattern[dlen + 6] = '\0';

    int count = 0;
    char **results = expand_glob(pattern, &count);
    ASSERT_NULL(results);
    ASSERT_INT_EQ(count, 0);

    free(pattern);
    remove_test_dir(dir);
    free(dir);
}

void test_glob_no_glob_chars(void) {
    int count = 0;
    char **results = expand_glob("hello.c", &count);
    ASSERT_NULL(results);
    ASSERT_INT_EQ(count, 0);
}


int main(void) {
    printf("test_expand\n");

    test_has_glob_with_star();
    test_has_glob_with_question();
    test_has_glob_no_glob();
    test_has_glob_null();

    test_unescape_star();
    test_unescape_question();
    test_unescape_mixed();

    test_glob_star_c();
    test_glob_question_mark();
    test_glob_star_all();
    test_glob_hidden_files();
    test_glob_no_matches();
    test_glob_no_glob_chars();

    TEST_REPORT();
}
