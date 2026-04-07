#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "history.h"
#include "util.h"

#define HISTORY_MAX 1000
#define HISTORY_LINE_MAX 4096

static char *entries[HISTORY_MAX];
static int num_entries = 0;
static char history_path[1024];
static int history_path_valid = 0;

// Build the history file path: ~/.config/splash/history
// Creates the directory if it doesn't exist.
static void init_history_path(void) {
    const char *home = getenv("HOME");
    if (!home) {
        return;
    }

    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/.config/splash", home);

    // Create directory if needed (mkdir -p equivalent for one level)
    struct stat st;
    if (stat(dir, &st) == -1) {
        char parent[1024];
        snprintf(parent, sizeof(parent), "%s/.config", home);
        if (stat(parent, &st) == -1) {
            if (mkdir(parent, 0755) == -1 && errno != EEXIST) {
                return;
            }
        }
        if (mkdir(dir, 0755) == -1 && errno != EEXIST) {
            return;
        }
    }

    snprintf(history_path, sizeof(history_path), "%s/history", dir);
    history_path_valid = 1;
}

// Load history entries from the persistent file.
static void history_load(void) {
    if (!history_path_valid) {
        return;
    }

    FILE *f = fopen(history_path, "r");
    if (!f) {
        return;  // No history file yet — that's fine
    }

    char line[HISTORY_LINE_MAX];
    while (fgets(line, (int)sizeof(line), f)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        if (line[0] == '\0') {
            continue;
        }

        // Add directly to avoid re-saving to file during load
        if (num_entries > 0 && strcmp(entries[num_entries - 1], line) == 0) {
            continue;
        }
        if (num_entries >= HISTORY_MAX) {
            free(entries[0]);
            for (int i = 0; i < num_entries - 1; i++) {
                entries[i] = entries[i + 1];
            }
            num_entries--;
        }
        entries[num_entries] = xstrdup(line);
        num_entries++;
    }

    fclose(f);
}

// Append a single line to the persistent history file.
static void history_append(const char *line) {
    if (!history_path_valid) {
        return;
    }

    FILE *f = fopen(history_path, "a");
    if (!f) {
        fprintf(stderr, "splash: history: open '%s': %s\n",
                history_path, strerror(errno));
        return;
    }
    if (fprintf(f, "%s\n", line) < 0) {
        fprintf(stderr, "splash: history: write: %s\n", strerror(errno));
    }
    fclose(f);
}

void history_init(int interactive) {
    num_entries = 0;
    if (interactive) {
        init_history_path();
        history_load();
    }
    // Non-interactive mode: leave history_path_valid = 0 so that
    // history_add() does not append to disk, and do not load existing
    // entries. history remains usable in-memory for the session.
}

void history_add(const char *line) {
    if (!line || *line == '\0') {
        return;
    }

    // Don't add duplicates of the last entry
    if (num_entries > 0 && strcmp(entries[num_entries - 1], line) == 0) {
        return;
    }

    if (num_entries >= HISTORY_MAX) {
        // Drop oldest entry
        free(entries[0]);
        for (int i = 0; i < num_entries - 1; i++) {
            entries[i] = entries[i + 1];
        }
        num_entries--;
    }

    entries[num_entries] = xstrdup(line);
    num_entries++;

    history_append(line);
}

void history_print(void) {
    for (int i = 0; i < num_entries; i++) {
        printf("%5d  %s\n", i + 1, entries[i]);
    }
}

int history_count(void) {
    return num_entries;
}

const char *history_get(int index) {
    if (index < 0 || index >= num_entries) {
        return NULL;
    }
    return entries[index];
}
