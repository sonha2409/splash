#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "editor.h"
#include "executor.h"
#include "expand.h"
#include "functions.h"
#include "history.h"
#include "jobs.h"
#include "signals.h"

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
        tcsetpgrp(STDIN_FILENO, shell_pgid);

        // Set up signal handlers
        signals_init();
    }

    jobs_init();
    history_init();

    // Initialize line editor (saves terminal state, registers atexit)
    if (interactive) {
        editor_init();
    }

    // Auto-source config files (interactive only)
    if (interactive) {
        const char *home = getenv("HOME");
        if (home) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/.config/splash/init.sh", home);
            source_if_exists(path);
            snprintf(path, sizeof(path), "%s/.shellrc", home);
            source_if_exists(path);
        }
    }

    for (;;) {
        if (interactive) {
            // Notify about completed background jobs
            jobs_notify();
        }

        char *line = editor_readline("splash> ");
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

        history_add(line);
        last_status = executor_execute_line(line);
        free(line);
    }

    functions_free_all();
    expand_free_params();

    (void)last_status;
    return 0;
}
