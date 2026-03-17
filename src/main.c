#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "command.h"
#include "executor.h"
#include "jobs.h"
#include "parser.h"
#include "signals.h"
#include "tokenizer.h"
#include "util.h"

#define MAX_INPUT_LINE 4096

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

        // Tokenize
        TokenList *tokens = tokenizer_tokenize(line);

        // Parse
        Pipeline *pl = parser_parse(tokens);
        if (pl) {
            last_status = executor_execute(pl, line);
            pipeline_free(pl);
        }

        token_list_free(tokens);
    }

    (void)last_status;
    return 0;
}
