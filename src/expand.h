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

// Push a positional parameter frame for function calls ($1..$N, $#, $@, $*).
// params is an array of argc strings (not owned — they are copied).
void expand_push_params(int argc, const char **params);

// Pop the most recent positional parameter frame.
void expand_pop_params(void);

// Free all positional parameter frames. Called at shell exit.
void expand_free_params(void);

// Returns 1 if currently executing inside a function, 0 otherwise.
int expand_in_function(void);

// Save the current value of a variable and set it to a new value.
// Called by the `local` builtin. Returns 0 on success, -1 if not in a function.
// If value is NULL, the variable is set to empty string.
int expand_save_local(const char *name, const char *value);

// Expand a variable name. Returns the value (not owned — do not free),
// or NULL if undefined. Handles special variables: ?, $, !, _, 0-9, #, @, *.
const char *expand_variable(const char *name);

// Expand a tilde prefix. Returns a newly allocated string.
// ~       → $HOME
// ~user   → user's home directory
// Returns NULL if expansion fails (caller should use original).
char *expand_tilde(const char *word);

// Sentinel bytes for unquoted glob characters (set by tokenizer).
#define GLOB_STAR  '\x01'
#define GLOB_QUEST '\x02'

// Check if a word contains glob sentinel bytes.
int expand_has_glob(const char *word);

// Expand a glob pattern (containing sentinel bytes) into matching filenames.
// Returns a newly allocated NULL-terminated array of strings, and sets *count.
// If no matches, returns NULL (caller should use the literal word).
// Caller must free each string and the array itself.
char **expand_glob(const char *pattern, int *count);

// Replace glob sentinel bytes back to their literal characters.
// Modifies the string in place.
void expand_glob_unescape(char *word);

// Execute a command and capture its stdout output.
// Returns a newly allocated string with trailing newlines stripped.
// Returns empty string on failure. Caller must free.
char *expand_command_subst(const char *cmd);

#include "command.h"

// Expand globs in a command's argv. Replaces each arg that contains glob
// sentinels with the matching filenames. If no matches, unescapes the sentinels
// back to literal characters. Also unescapes non-glob args.
void expand_glob_argv(SimpleCommand *cmd);

#endif // SPLASH_EXPAND_H
