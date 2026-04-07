#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "editor.h"
#include "executor.h"
#include "expand.h"
#include "functions.h"
#include "history.h"
#include "jobs.h"
#include "signals.h"
#include "util.h"

// Extract heredoc delimiter from a line containing <<.
// Returns the delimiter string (caller frees) or NULL if no heredoc found.
// Sets *strip_tabs if <<- is used.
static char *find_heredoc_delim(const char *line, int *strip_tabs) {
    const char *p = line;
    *strip_tabs = 0;
    while (*p) {
        // Skip inside single-quoted strings
        if (*p == '\'') {
            p++;
            while (*p && *p != '\'') p++;
            if (*p) p++;
            continue;
        }
        // Skip inside double-quoted strings
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p++;
                p++;
            }
            if (*p) p++;
            continue;
        }
        if (p[0] == '<' && p[1] == '<' && p[2] != '<') {
            p += 2;
            if (*p == '-') {
                *strip_tabs = 1;
                p++;
            }
            while (*p == ' ' || *p == '\t') p++;
            // Read delimiter (handle quotes)
            if (*p == '\'' || *p == '"') {
                char qc = *p++;
                const char *ds = p;
                while (*p && *p != qc) p++;
                size_t dlen = (size_t)(p - ds);
                char *delim = xmalloc(dlen + 1);
                memcpy(delim, ds, dlen);
                delim[dlen] = '\0';
                return delim;
            }
            // Unquoted delimiter
            const char *ds = p;
            while (*p && !isspace((unsigned char)*p) && *p != ';' &&
                   *p != '|' && *p != '&' && *p != ')') {
                p++;
            }
            if (p > ds) {
                size_t dlen = (size_t)(p - ds);
                char *delim = xmalloc(dlen + 1);
                memcpy(delim, ds, dlen);
                delim[dlen] = '\0';
                return delim;
            }
            return NULL;
        }
        p++;
    }
    return NULL;
}

// Read heredoc body lines after a line containing <<DELIM.
// Returns a new string: original_line + '\n' + body_lines + delimiter_line.
// Caller must free.
static char *collect_heredoc(const char *first_line, const char *prompt) {
    int strip_tabs = 0;
    char *delim = find_heredoc_delim(first_line, &strip_tabs);
    if (!delim) {
        return xstrdup(first_line);
    }

    // Start building: first_line + '\n' + body + delim_line
    size_t cap = strlen(first_line) + 256;
    char *buf = xmalloc(cap);
    size_t len = strlen(first_line);
    memcpy(buf, first_line, len);
    buf[len++] = '\n';
    buf[len] = '\0';

    for (;;) {
        char *body_line = editor_readline(prompt);
        if (!body_line) {
            fprintf(stderr, "splash: warning: here-document "
                    "delimited by '%s' not found\n", delim);
            break;
        }
        // Check if this line is the delimiter
        const char *check = body_line;
        if (strip_tabs) {
            while (*check == '\t') check++;
        }
        int is_delim = (strcmp(check, delim) == 0);

        // Append line + newline to buf
        size_t llen = strlen(body_line);
        while (len + llen + 2 >= cap) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
        memcpy(buf + len, body_line, llen);
        len += llen;
        buf[len++] = '\n';
        buf[len] = '\0';
        free(body_line);

        if (is_delim) {
            break;
        }
    }

    free(delim);
    return buf;
}

// Source a config file if it exists and is readable.
static void source_if_exists(const char *path) {
    if (access(path, R_OK) == 0) {
        char cmd[4096 + 8];
        snprintf(cmd, sizeof(cmd), "source %s", path);
        executor_execute_line(cmd);
    }
}

int main(void) {
    int last_status = 0;
    int interactive = isatty(STDIN_FILENO);

    // Shell takes control of the terminal
    if (interactive) {
        // Put shell in its own process group
        pid_t shell_pgid = getpid();
        if (setpgid(shell_pgid, shell_pgid) == -1) {
            // May already be group leader — not an error
        }
        jobs_set_shell_pgid(shell_pgid);
        if (tcsetpgrp(STDIN_FILENO, shell_pgid) == -1 && errno != ENOTTY) {
            fprintf(stderr, "splash: tcsetpgrp (init): %s\n",
                    strerror(errno));
        }

        // Set up signal handlers
        signals_init();
    }

    jobs_init();
    history_init();

    // Initialize line editor (saves terminal state, registers atexit)
    if (interactive) {
        editor_init();
    }

    // Initialize config directory and load config.toml
    config_init();
    config_load();

    // Auto-source config files (interactive only)
    if (interactive) {
        const char *config_dir = config_get_dir();
        if (config_dir) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/init.sh", config_dir);
            source_if_exists(path);
        }
        const char *home = getenv("HOME");
        if (home) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/.shellrc", home);
            source_if_exists(path);
        }
    }

    for (;;) {
        if (interactive) {
            // Notify about completed background jobs
            jobs_notify();
        }

        char *prompt = config_build_prompt();
        char *line = editor_readline(prompt);
        free(prompt);
        if (!line) {
            if (interactive) {
                // Write newline since we're in raw mode
                printf("Viszontlátásra!!\n");
                printf("Jó egészséget és sok szerencsét kívánok!\n");
            }
            break;
        }

        // Skip empty lines
        if (line[0] == '\0') {
            free(line);
            continue;
        }

        // If line contains a heredoc, read body lines
        int dummy;
        char *hd_delim = find_heredoc_delim(line, &dummy);
        char *effective = line;
        if (hd_delim) {
            free(hd_delim);
            effective = collect_heredoc(line, interactive ? "> " : "");
            free(line);
            line = effective;
        }

        history_add(line);
        last_status = executor_execute_line(line);
        free(line);

        // ON_ERROR: print message when last command failed
        if (last_status != 0 && interactive) {
            const char *on_error = getenv("ON_ERROR");
            if (on_error && on_error[0] != '\0') {
                fprintf(stderr, "%s\n", on_error);
            }
        }
    }

    functions_free_all();
    expand_free_params();

    (void)last_status;
    return 0;
}
