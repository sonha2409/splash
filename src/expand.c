#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "executor.h"
#include "expand.h"
#include "signals.h"
#include "util.h"

static int last_status = 0;
static int last_bg_pid = 0;
static char *last_arg = NULL;

// Static buffers for special variable string conversion
static char status_buf[16];
static char pid_buf[16];
static char bg_pid_buf[16];
static char param_count_buf[16];

// Saved local variable (for restoring on function return)
typedef struct SavedLocal {
    char *name;            // Variable name (owned)
    char *old_value;       // Previous value (owned), or NULL if was unset
} SavedLocal;

// Positional parameter stack for function calls
typedef struct ParamFrame {
    char **params;         // Array of parameter strings (owned copies)
    int count;             // Number of parameters
    char *joined;          // Cached "$*" / "$@" expansion (owned, lazily built)
    SavedLocal *locals;    // Array of saved local variables
    int num_locals;        // Number of saved locals
    int locals_capacity;   // Capacity of locals array
    struct ParamFrame *prev;
} ParamFrame;

static ParamFrame *param_stack = NULL;

void expand_push_params(int argc, const char **params) {
    ParamFrame *frame = xmalloc(sizeof(ParamFrame));
    frame->count = argc;
    frame->params = xmalloc(sizeof(char *) * (size_t)(argc > 0 ? argc : 1));
    for (int i = 0; i < argc; i++) {
        frame->params[i] = xstrdup(params[i]);
    }
    frame->joined = NULL;
    frame->locals = NULL;
    frame->num_locals = 0;
    frame->locals_capacity = 0;
    frame->prev = param_stack;
    param_stack = frame;
}

void expand_pop_params(void) {
    if (!param_stack) {
        return;
    }
    ParamFrame *frame = param_stack;
    param_stack = frame->prev;

    // Restore saved local variables (in reverse order for correctness)
    for (int i = frame->num_locals - 1; i >= 0; i--) {
        SavedLocal *sl = &frame->locals[i];
        if (sl->old_value) {
            setenv(sl->name, sl->old_value, 1);
            free(sl->old_value);
        } else {
            unsetenv(sl->name);
        }
        free(sl->name);
    }
    free(frame->locals);

    for (int i = 0; i < frame->count; i++) {
        free(frame->params[i]);
    }
    free(frame->params);
    free(frame->joined);
    free(frame);
}

void expand_free_params(void) {
    while (param_stack) {
        expand_pop_params();
    }
}

// Build a space-separated string from the current param frame.
static const char *get_joined_params(void) {
    if (!param_stack || param_stack->count == 0) {
        return "";
    }
    if (param_stack->joined) {
        return param_stack->joined;
    }
    size_t total = 0;
    for (int i = 0; i < param_stack->count; i++) {
        total += strlen(param_stack->params[i]);
        if (i > 0) total++; // space separator
    }
    param_stack->joined = xmalloc(total + 1);
    char *p = param_stack->joined;
    for (int i = 0; i < param_stack->count; i++) {
        if (i > 0) *p++ = ' ';
        size_t len = strlen(param_stack->params[i]);
        memcpy(p, param_stack->params[i], len);
        p += len;
    }
    *p = '\0';
    return param_stack->joined;
}

int expand_in_function(void) {
    return param_stack != NULL;
}

static int return_pending_flag = 0;

void expand_set_return_pending(int pending) {
    return_pending_flag = pending;
}

int expand_return_pending(void) {
    return return_pending_flag;
}

int expand_save_local(const char *name, const char *value) {
    if (!param_stack) {
        return -1; // Not in a function
    }

    // Check if already saved in this frame (don't double-save)
    for (int i = 0; i < param_stack->num_locals; i++) {
        if (strcmp(param_stack->locals[i].name, name) == 0) {
            // Already saved — just set the new value
            if (value) {
                setenv(name, value, 1);
            } else {
                setenv(name, "", 1);
            }
            return 0;
        }
    }

    // Save current value
    if (param_stack->num_locals >= param_stack->locals_capacity) {
        int new_cap = param_stack->locals_capacity == 0 ? 4
                      : param_stack->locals_capacity * 2;
        param_stack->locals = xrealloc(param_stack->locals,
                                       sizeof(SavedLocal) * (size_t)new_cap);
        param_stack->locals_capacity = new_cap;
    }

    SavedLocal *sl = &param_stack->locals[param_stack->num_locals++];
    sl->name = xstrdup(name);
    const char *old = getenv(name);
    sl->old_value = old ? xstrdup(old) : NULL;

    // Set the new value
    if (value) {
        setenv(name, value, 1);
    } else {
        setenv(name, "", 1);
    }
    return 0;
}

void expand_set_last_status(int status) {
    last_status = status;
}

int expand_get_last_status(void) {
    return last_status;
}

void expand_set_last_bg_pid(int pid) {
    last_bg_pid = pid;
}

void expand_set_last_arg(const char *arg) {
    free(last_arg);
    last_arg = arg ? xstrdup(arg) : NULL;
}

const char *expand_variable(const char *name) {
    if (!name || *name == '\0') {
        return NULL;
    }

    // Special variables (single character)
    if (name[1] == '\0') {
        switch (name[0]) {
            case '?':
                snprintf(status_buf, sizeof(status_buf), "%d", last_status);
                return status_buf;
            case '$':
                snprintf(pid_buf, sizeof(pid_buf), "%d", getpid());
                return pid_buf;
            case '!':
                if (last_bg_pid == 0) {
                    return "";
                }
                snprintf(bg_pid_buf, sizeof(bg_pid_buf), "%d", last_bg_pid);
                return bg_pid_buf;
            case '_':
                return last_arg ? last_arg : "";
            case '#':
                if (param_stack) {
                    snprintf(param_count_buf, sizeof(param_count_buf),
                             "%d", param_stack->count);
                    return param_count_buf;
                }
                return "0";
            case '@':
            case '*':
                return get_joined_params();
            case '0':
                return "splash";
        }
        // Positional parameters $1-$9
        if (name[0] >= '1' && name[0] <= '9') {
            int idx = name[0] - '1'; // $1 → index 0
            if (param_stack && idx < param_stack->count) {
                return param_stack->params[idx];
            }
            return "";
        }
    }

    // Multi-digit positional parameters ($10, $11, ...)
    if (name[0] >= '1' && name[0] <= '9') {
        int all_digits = 1;
        for (int i = 0; name[i]; i++) {
            if (name[i] < '0' || name[i] > '9') {
                all_digits = 0;
                break;
            }
        }
        if (all_digits) {
            int idx = atoi(name) - 1;
            if (param_stack && idx >= 0 && idx < param_stack->count) {
                return param_stack->params[idx];
            }
            return "";
        }
    }

    return getenv(name);
}

char *expand_tilde(const char *word) {
    if (!word || word[0] != '~') {
        return NULL;
    }

    // ~ or ~/...
    if (word[1] == '\0' || word[1] == '/') {
        const char *home = getenv("HOME");
        if (!home) {
            return NULL;
        }
        size_t hlen = strlen(home);
        size_t rlen = strlen(word + 1); // includes the / if present
        char *result = xmalloc(hlen + rlen + 1);
        memcpy(result, home, hlen);
        memcpy(result + hlen, word + 1, rlen);
        result[hlen + rlen] = '\0';
        return result;
    }

    // ~user or ~user/...
    const char *slash = strchr(word + 1, '/');
    size_t ulen = slash ? (size_t)(slash - word - 1) : strlen(word + 1);
    char username[256];
    if (ulen >= sizeof(username)) {
        return NULL;
    }
    memcpy(username, word + 1, ulen);
    username[ulen] = '\0';

    struct passwd *pw = getpwnam(username);
    if (!pw) {
        return NULL;
    }

    size_t hlen = strlen(pw->pw_dir);
    const char *rest = slash ? slash : "";
    size_t rlen = strlen(rest);
    char *result = xmalloc(hlen + rlen + 1);
    memcpy(result, pw->pw_dir, hlen);
    memcpy(result + hlen, rest, rlen);
    result[hlen + rlen] = '\0';
    return result;
}


char *expand_command_subst(const char *cmd) {
    if (!cmd || *cmd == '\0') {
        return xstrdup("");
    }

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        fprintf(stderr, "splash: command substitution pipe: %s\n",
                strerror(errno));
        return xstrdup("");
    }

    pid_t pid = fork();
    if (pid == -1) {
        fprintf(stderr, "splash: command substitution fork: %s\n",
                strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return xstrdup("");
    }

    if (pid == 0) {
        // Child: redirect stdout to pipe, execute command
        close(pipefd[0]);

        // Redirect stdin from /dev/null to disable job control
        // (isatty(STDIN_FILENO) will return false, preventing tcsetpgrp)
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            if (dup2(devnull, STDIN_FILENO) == -1) {
                fprintf(stderr, "splash: command substitution: dup2 /dev/null: %s\n",
                        strerror(errno));
                close(devnull);
                _exit(1);
            }
            close(devnull);
        }

        if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
            fprintf(stderr, "splash: command substitution: dup2 stdout: %s\n",
                    strerror(errno));
            close(pipefd[1]);
            _exit(1);
        }
        close(pipefd[1]);

        signals_default();
        int status = executor_execute_line(cmd);
        fflush(stdout);
        _exit(status);
    }

    // Parent: read all output from child
    close(pipefd[1]);

    size_t capacity = 256;
    size_t len = 0;
    char *output = xmalloc(capacity);

    ssize_t n;
    char buf[4096];
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        while (len + (size_t)n >= capacity) {
            capacity *= 2;
            output = xrealloc(output, capacity);
        }
        memcpy(output + len, buf, (size_t)n);
        len += (size_t)n;
    }
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        expand_set_last_status(WEXITSTATUS(status));
    }

    // Strip trailing newlines
    while (len > 0 && output[len - 1] == '\n') {
        len--;
    }
    output[len] = '\0';

    return output;
}


int expand_has_glob(const char *word) {
    if (!word) {
        return 0;
    }
    for (const char *p = word; *p; p++) {
        if (*p == GLOB_STAR || *p == GLOB_QUEST) {
            return 1;
        }
    }
    return 0;
}

void expand_glob_unescape(char *word) {
    if (!word) {
        return;
    }
    for (char *p = word; *p; p++) {
        if (*p == GLOB_STAR) {
            *p = '*';
        } else if (*p == GLOB_QUEST) {
            *p = '?';
        }
    }
}

// Match a pattern (with GLOB_STAR/GLOB_QUEST sentinels) against a string.
// Returns 1 on match, 0 on mismatch.
static int glob_match(const char *pattern, const char *str) {
    const char *p = pattern;
    const char *s = str;
    const char *star_p = NULL;
    const char *star_s = NULL;

    while (*s) {
        if (*p == GLOB_QUEST) {
            // Match any single character
            p++;
            s++;
        } else if (*p == GLOB_STAR) {
            // Record star position and try matching zero characters
            star_p = p;
            star_s = s;
            p++;
        } else if (*p == *s) {
            p++;
            s++;
        } else if (star_p) {
            // Backtrack: star matches one more character
            p = star_p + 1;
            star_s++;
            s = star_s;
        } else {
            return 0;
        }
    }

    // Skip trailing stars
    while (*p == GLOB_STAR) {
        p++;
    }

    return *p == '\0';
}

// Compare function for qsort of strings.
static int str_compare(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

char **expand_glob(const char *pattern, int *count) {
    *count = 0;

    if (!pattern || !expand_has_glob(pattern)) {
        return NULL;
    }

    // Split pattern into directory and filename parts at last /
    const char *last_slash = NULL;
    for (const char *p = pattern; *p; p++) {
        if (*p == '/') {
            last_slash = p;
        }
    }

    char *dir_path = NULL;
    const char *file_pattern = NULL;

    if (last_slash) {
        size_t dir_len = (size_t)(last_slash - pattern);
        dir_path = xmalloc(dir_len + 1);
        memcpy(dir_path, pattern, dir_len);
        dir_path[dir_len] = '\0';
        // Unescape the directory part (globs in dir not supported yet)
        expand_glob_unescape(dir_path);
        file_pattern = last_slash + 1;
    } else {
        dir_path = xstrdup(".");
        file_pattern = pattern;
    }

    DIR *d = opendir(dir_path);
    if (!d) {
        free(dir_path);
        return NULL;
    }

    // Collect matches
    int capacity = 16;
    char **results = xmalloc(sizeof(char *) * (size_t)capacity);

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        const char *name = entry->d_name;

        // Skip hidden files unless pattern starts with '.'
        if (name[0] == '.' && file_pattern[0] != '.') {
            continue;
        }

        if (glob_match(file_pattern, name)) {
            if (*count >= capacity) {
                capacity *= 2;
                results = xrealloc(results,
                                   sizeof(char *) * (size_t)capacity);
            }

            // Build full path if pattern had a directory component
            if (last_slash) {
                size_t dlen = strlen(dir_path);
                size_t nlen = strlen(name);
                char *full = xmalloc(dlen + 1 + nlen + 1);
                memcpy(full, dir_path, dlen);
                full[dlen] = '/';
                memcpy(full + dlen + 1, name, nlen);
                full[dlen + 1 + nlen] = '\0';
                results[*count] = full;
            } else {
                results[*count] = xstrdup(name);
            }
            (*count)++;
        }
    }

    closedir(d);
    free(dir_path);

    if (*count == 0) {
        free(results);
        return NULL;
    }

    // Sort results alphabetically
    qsort(results, (size_t)*count, sizeof(char *), str_compare);

    return results;
}

void expand_glob_argv(SimpleCommand *cmd) {
    if (!cmd || cmd->argc == 0) {
        return;
    }

    // Build a new argv by expanding each arg
    int new_capacity = cmd->argc * 2;
    char **new_argv = xmalloc(sizeof(char *) * (size_t)(new_capacity + 1));
    int new_argc = 0;

    for (int i = 0; i < cmd->argc; i++) {
        if (expand_has_glob(cmd->argv[i])) {
            int match_count = 0;
            char **matches = expand_glob(cmd->argv[i], &match_count);
            if (matches) {
                // Ensure capacity
                while (new_argc + match_count >= new_capacity) {
                    new_capacity *= 2;
                    new_argv = xrealloc(new_argv,
                                        sizeof(char *) * (size_t)(new_capacity + 1));
                }
                for (int j = 0; j < match_count; j++) {
                    new_argv[new_argc++] = matches[j]; // transfer ownership
                }
                free(matches); // free array, not strings
                free(cmd->argv[i]); // free original glob pattern
            } else {
                // No matches — unescape and keep literal
                expand_glob_unescape(cmd->argv[i]);
                if (new_argc >= new_capacity) {
                    new_capacity *= 2;
                    new_argv = xrealloc(new_argv,
                                        sizeof(char *) * (size_t)(new_capacity + 1));
                }
                new_argv[new_argc++] = cmd->argv[i]; // keep original
            }
        } else {
            // No glob chars — unescape any sentinels (shouldn't have any, but safe)
            expand_glob_unescape(cmd->argv[i]);
            if (new_argc >= new_capacity) {
                new_capacity *= 2;
                new_argv = xrealloc(new_argv,
                                    sizeof(char *) * (size_t)(new_capacity + 1));
            }
            new_argv[new_argc++] = cmd->argv[i]; // keep original
        }
    }

    new_argv[new_argc] = NULL;

    // Replace command's argv
    free(cmd->argv); // free old array (strings were either transferred or freed)
    cmd->argv = new_argv;
    cmd->argc = new_argc;
    cmd->argv_capacity = new_capacity + 1;
}
