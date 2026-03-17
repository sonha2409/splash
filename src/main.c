#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "executor.h"
#include "history.h"
#include "jobs.h"
#include "signals.h"

#define MAX_INPUT_LINE 4096

// Source a config file if it exists and is readable.
static void source_if_exists(const char *path) {
    if (access(path, R_OK) == 0) {
        char cmd[4096 + 8];
        snprintf(cmd, sizeof(cmd), "source %s", path);
        executor_execute_line(cmd);
    }
}

int main(void) {
    char line[MAX_INPUT_LINE];
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

            printf("splash> ");
            fflush(stdout);
        }

        if (!fgets(line, sizeof(line), stdin)) {
            if (interactive) {
                printf("\nViszontlátásra!!\n");
                printf("Jó egészséget és sok szerencsét kívánok!\n");
            }
            break;
        }

        // Strip trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }

        // Skip empty lines
        if (len == 0) {
            continue;
        }

        history_add(line);
        last_status = executor_execute_line(line);
    }

    (void)last_status;
    return 0;
}
