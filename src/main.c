#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define MAX_INPUT_LINE 4096

int main(void) {
    char line[MAX_INPUT_LINE];

    for (;;) {
        printf("splash> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\nGood bye!!\n");
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

        // Placeholder: echo back the input until we have a tokenizer
        printf("[debug] input: %s\n", line);
    }

    return 0;
}
