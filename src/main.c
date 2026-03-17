#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "command.h"
#include "executor.h"
#include "parser.h"
#include "tokenizer.h"
#include "util.h"

#define MAX_INPUT_LINE 4096

int main(void) {
    char line[MAX_INPUT_LINE];
    int last_status = 0;
    int interactive = isatty(STDIN_FILENO);

    for (;;) {
        if (interactive) {
            printf("splash> ");
            fflush(stdout);
        }

        if (!fgets(line, sizeof(line), stdin)) {
            if (interactive) {
                printf("\nGood bye!!\n");
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
            last_status = executor_execute(pl);
            pipeline_free(pl);
        }

        token_list_free(tokens);
    }

    (void)last_status;
    return 0;
}
