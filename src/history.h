#ifndef SPLASH_HISTORY_H
#define SPLASH_HISTORY_H

// Initialize the history buffer and load persistent history from disk.
void history_init(void);

// Add a line to history. The string is copied.
// Also appends to the persistent history file.
void history_add(const char *line);

// Print all history entries with line numbers to stdout.
void history_print(void);

// Return the number of entries in history.
int history_count(void);

// Return the history entry at the given index (0-based).
// Returns NULL if index is out of range. The returned string is owned
// by the history module — caller must not free it.
const char *history_get(int index);

#endif // SPLASH_HISTORY_H
