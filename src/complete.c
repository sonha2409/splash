#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "complete.h"
#include "util.h"

#define INITIAL_CAPACITY 16

static CompletionResult *result_new(void) {
    CompletionResult *r = xmalloc(sizeof(CompletionResult));
    r->matches = xmalloc(sizeof(char *) * INITIAL_CAPACITY);
    r->count = 0;
    r->capacity = INITIAL_CAPACITY;
    return r;
}

static void result_add(CompletionResult *r, const char *match) {
    if (r->count >= r->capacity) {
        r->capacity *= 2;
        r->matches = xrealloc(r->matches,
                               sizeof(char *) * (size_t)r->capacity);
    }
    r->matches[r->count++] = xstrdup(match);
}

void completion_result_free(CompletionResult *result) {
    if (!result) {
        return;
    }
    for (int i = 0; i < result->count; i++) {
        free(result->matches[i]);
    }
    free(result->matches);
    free(result);
}

char *completion_common_prefix(const CompletionResult *result) {
    if (!result || result->count == 0) {
        return NULL;
    }
    if (result->count == 1) {
        return xstrdup(result->matches[0]);
    }

    // Start with the first match, shorten as needed
    size_t prefix_len = strlen(result->matches[0]);
    for (int i = 1; i < result->count; i++) {
        size_t j = 0;
        while (j < prefix_len && result->matches[i][j] != '\0' &&
               result->matches[0][j] == result->matches[i][j]) {
            j++;
        }
        prefix_len = j;
    }

    char *prefix = xmalloc(prefix_len + 1);
    memcpy(prefix, result->matches[0], prefix_len);
    prefix[prefix_len] = '\0';
    return prefix;
}

// Compare function for qsort of strings.
static int cmp_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

CompletionResult *complete_path(const char *prefix) {
    CompletionResult *result = result_new();

    // Split prefix into directory part and filename part
    const char *last_slash = strrchr(prefix, '/');
    char *dir_path = NULL;
    const char *file_prefix;
    const char *display_dir; // prefix to prepend to matches
    size_t display_dir_len;

    if (last_slash) {
        size_t dir_len = (size_t)(last_slash - prefix) + 1; // include the /
        dir_path = xmalloc(dir_len + 1);
        memcpy(dir_path, prefix, dir_len);
        dir_path[dir_len] = '\0';
        file_prefix = last_slash + 1;
        display_dir = dir_path;
        display_dir_len = dir_len;
    } else {
        file_prefix = prefix;
        display_dir = "";
        display_dir_len = 0;
    }

    // Resolve the actual directory to open
    // Handle tilde expansion for opendir
    char *open_path = NULL;
    if (dir_path && dir_path[0] == '~') {
        const char *home = getenv("HOME");
        if (home) {
            // Replace ~ with $HOME
            size_t home_len = strlen(home);
            size_t rest_len = strlen(dir_path) - 1; // skip ~
            open_path = xmalloc(home_len + rest_len + 1);
            memcpy(open_path, home, home_len);
            memcpy(open_path + home_len, dir_path + 1, rest_len);
            open_path[home_len + rest_len] = '\0';
        }
    } else if (!dir_path && prefix[0] == '~') {
        // Tilde with no slash yet — could complete ~/
        // For now, just use prefix as-is
    }

    const char *actual_dir;
    if (open_path) {
        actual_dir = open_path;
    } else if (dir_path) {
        actual_dir = dir_path;
    } else {
        actual_dir = ".";
    }

    size_t file_prefix_len = strlen(file_prefix);

    DIR *d = opendir(actual_dir);
    if (!d) {
        free(dir_path);
        free(open_path);
        return result;
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        const char *name = entry->d_name;

        // Skip . and .. unless the prefix starts with .
        if (name[0] == '.') {
            if (file_prefix_len == 0 || file_prefix[0] != '.') {
                continue;
            }
            // Always skip . and .. themselves
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                continue;
            }
        }

        // Check if name starts with file_prefix
        if (file_prefix_len > 0 &&
            strncmp(name, file_prefix, file_prefix_len) != 0) {
            continue;
        }

        // Build the full match string: display_dir + name [+ '/']
        size_t name_len = strlen(name);
        int is_dir = 0;

        // Check if it's a directory
        size_t full_path_len = strlen(actual_dir) + 1 + name_len + 1;
        char *full_path = xmalloc(full_path_len);
        snprintf(full_path, full_path_len, "%s/%s", actual_dir, name);

        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            is_dir = 1;
        }
        free(full_path);

        // Build result: display_dir + name + optional /
        size_t match_len = display_dir_len + name_len + (is_dir ? 1 : 0);
        char *match = xmalloc(match_len + 1);
        memcpy(match, display_dir, display_dir_len);
        memcpy(match + display_dir_len, name, name_len);
        if (is_dir) {
            match[display_dir_len + name_len] = '/';
        }
        match[match_len] = '\0';

        result_add(result, match);
        free(match);
    }
    closedir(d);

    // Sort matches alphabetically
    if (result->count > 1) {
        qsort(result->matches, (size_t)result->count, sizeof(char *),
              cmp_strings);
    }

    free(dir_path);
    free(open_path);
    return result;
}
