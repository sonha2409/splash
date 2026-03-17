#ifndef SPLASH_EXPAND_H
#define SPLASH_EXPAND_H

// Set the last exit status (for $? expansion).
void expand_set_last_status(int status);

// Get the last exit status.
int expand_get_last_status(void);

// Set the last background PID (for $! expansion).
void expand_set_last_bg_pid(int pid);

// Set the last argument of the previous command (for $_ expansion).
// The string is copied.
void expand_set_last_arg(const char *arg);

// Expand a variable name. Returns the value (not owned — do not free),
// or NULL if undefined. Handles special variables: ?, $, !, _.
const char *expand_variable(const char *name);

// Expand a tilde prefix. Returns a newly allocated string.
// ~       → $HOME
// ~user   → user's home directory
// Returns NULL if expansion fails (caller should use original).
char *expand_tilde(const char *word);

#endif // SPLASH_EXPAND_H
