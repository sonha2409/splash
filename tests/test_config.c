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

int main(void) {
    printf("test_config:\n");

    // Save original env
    const char *orig_home = getenv("HOME");
    char *saved_home = orig_home ? strdup(orig_home) : NULL;
    const char *orig_xdg = getenv("XDG_CONFIG_HOME");
    char *saved_xdg = orig_xdg ? strdup(orig_xdg) : NULL;

    test_config_init_creates_dir();
    test_config_init_xdg();
    test_config_init_xdg_empty();
    test_config_init_no_home();
    test_config_init_existing_dir();

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
