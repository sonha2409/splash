#ifndef SPLASH_HISTORY_H
#define SPLASH_HISTORY_H

// Initialize the history buffer. When interactive is non-zero, also
// loads persistent history from disk and enables appending to the
// history file on each history_add(). When interactive is zero,
// history stays purely in-memory for the session (used by scripts
// and tests so they don't read or pollute the user's history file).
void history_init(int interactive);

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
