#ifndef SPLASH_HISTORY_H
#define SPLASH_HISTORY_H

// Initialize the history buffer.
void history_init(void);

// Add a line to history. The string is copied.
void history_add(const char *line);

// Print all history entries with line numbers to stdout.
void history_print(void);

// Return the number of entries in history.
int history_count(void);

#endif // SPLASH_HISTORY_H
