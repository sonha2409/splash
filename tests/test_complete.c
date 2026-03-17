#include "test.h"
#include "complete.h"

#include <sys/stat.h>
#include <unistd.h>

// Helper: check if a match is present in the result
static int has_match(CompletionResult *cr, const char *name) {
    for (int i = 0; i < cr->count; i++) {
        if (strcmp(cr->matches[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void test_complete_empty_prefix(void) {
    // Completing "" should return files in current directory
    CompletionResult *cr = complete_path("");
    ASSERT_NOT_NULL(cr);
    ASSERT(cr->count > 0); // current dir should have some files
    completion_result_free(cr);
}

static void test_complete_nonexistent_dir(void) {
    CompletionResult *cr = complete_path("/nonexistent_dir_xyz/foo");
    ASSERT_NOT_NULL(cr);
    ASSERT_INT_EQ(cr->count, 0);
    completion_result_free(cr);
}

static void test_complete_no_matches(void) {
    CompletionResult *cr = complete_path("zzz_no_such_file_xyz");
    ASSERT_NOT_NULL(cr);
    ASSERT_INT_EQ(cr->count, 0);
    completion_result_free(cr);
}

static void test_complete_src_directory(void) {
    // "src/" should list files in the src directory
    CompletionResult *cr = complete_path("src/");
    ASSERT_NOT_NULL(cr);
    ASSERT(cr->count > 0);
    // Should contain some known files
    ASSERT(has_match(cr, "src/editor.c"));
    ASSERT(has_match(cr, "src/highlight.c"));
    completion_result_free(cr);
}

static void test_complete_partial_filename(void) {
    // "src/ed" should match "src/editor.c" and "src/editor.h"
    CompletionResult *cr = complete_path("src/ed");
    ASSERT_NOT_NULL(cr);
    ASSERT_INT_EQ(cr->count, 2);
    ASSERT(has_match(cr, "src/editor.c"));
    ASSERT(has_match(cr, "src/editor.h"));
    completion_result_free(cr);
}

static void test_complete_unique_match(void) {
    // "src/complete.c" should be a unique match for "src/complete.c"
    // Let's test "src/comple" — should match complete.c and complete.h
    CompletionResult *cr = complete_path("src/comple");
    ASSERT_NOT_NULL(cr);
    ASSERT_INT_EQ(cr->count, 2);
    ASSERT(has_match(cr, "src/complete.c"));
    ASSERT(has_match(cr, "src/complete.h"));
    completion_result_free(cr);
}

static void test_complete_common_prefix(void) {
    // "src/ed" matches editor.c and editor.h
    // Common prefix should be "src/editor."
    CompletionResult *cr = complete_path("src/ed");
    ASSERT_NOT_NULL(cr);
    char *common = completion_common_prefix(cr);
    ASSERT_NOT_NULL(common);
    ASSERT_STR_EQ(common, "src/editor.");
    free(common);
    completion_result_free(cr);
}

static void test_complete_directory_has_trailing_slash(void) {
    // Completing "sr" should give "src/" with trailing slash
    CompletionResult *cr = complete_path("sr");
    ASSERT_NOT_NULL(cr);
    ASSERT(cr->count >= 1);
    ASSERT(has_match(cr, "src/"));
    completion_result_free(cr);
}

static void test_complete_hidden_files_not_shown(void) {
    // Completing "" should not include hidden files (starting with .)
    CompletionResult *cr = complete_path("");
    ASSERT_NOT_NULL(cr);
    for (int i = 0; i < cr->count; i++) {
        ASSERT(cr->matches[i][0] != '.');
    }
    completion_result_free(cr);
}

static void test_complete_hidden_files_with_dot_prefix(void) {
    // Completing "." should include hidden files (but not . and ..)
    CompletionResult *cr = complete_path(".");
    ASSERT_NOT_NULL(cr);
    // Should include .gitignore or .git/ if they exist, but not "." or ".."
    for (int i = 0; i < cr->count; i++) {
        ASSERT(strcmp(cr->matches[i], ".") != 0);
        ASSERT(strcmp(cr->matches[i], "..") != 0);
    }
    completion_result_free(cr);
}

static void test_complete_results_sorted(void) {
    // Results should be alphabetically sorted
    CompletionResult *cr = complete_path("src/");
    ASSERT_NOT_NULL(cr);
    for (int i = 1; i < cr->count; i++) {
        ASSERT(strcmp(cr->matches[i - 1], cr->matches[i]) < 0);
    }
    completion_result_free(cr);
}

static void test_complete_common_prefix_single_match(void) {
    // Single match: common prefix should be the match itself
    CompletionResult *cr = complete_path("Makefil");
    ASSERT_NOT_NULL(cr);
    ASSERT_INT_EQ(cr->count, 1);
    ASSERT_STR_EQ(cr->matches[0], "Makefile");
    char *common = completion_common_prefix(cr);
    ASSERT_STR_EQ(common, "Makefile");
    free(common);
    completion_result_free(cr);
}

static void test_complete_common_prefix_no_matches(void) {
    CompletionResult *cr = complete_path("zzz_nothing");
    ASSERT_NOT_NULL(cr);
    char *common = completion_common_prefix(cr);
    ASSERT_NULL(common);
    completion_result_free(cr);
}

int main(void) {
    printf("test_complete\n");

    // Tests must be run from the project root directory
    test_complete_empty_prefix();
    test_complete_nonexistent_dir();
    test_complete_no_matches();
    test_complete_src_directory();
    test_complete_partial_filename();
    test_complete_unique_match();
    test_complete_common_prefix();
    test_complete_directory_has_trailing_slash();
    test_complete_hidden_files_not_shown();
    test_complete_hidden_files_with_dot_prefix();
    test_complete_results_sorted();
    test_complete_common_prefix_single_match();
    test_complete_common_prefix_no_matches();

    TEST_REPORT();
}
