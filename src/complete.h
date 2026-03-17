#ifndef SPLASH_COMPLETE_H
#define SPLASH_COMPLETE_H

typedef struct {
    char **matches;  // Array of completion strings (owned)
    int count;
    int capacity;
} CompletionResult;

// Complete a file/directory path prefix.
// Returns all matching paths. Directories have a trailing '/'.
// Caller takes ownership and must call completion_result_free().
CompletionResult *complete_path(const char *prefix);

// Complete a command name prefix from builtins, aliases, and $PATH.
// Caller takes ownership and must call completion_result_free().
CompletionResult *complete_command(const char *prefix);

// Free a CompletionResult and all its matches.
void completion_result_free(CompletionResult *result);

// Find the longest common prefix among all matches.
// Returns malloc'd string. Caller takes ownership.
// Returns NULL if no matches.
char *completion_common_prefix(const CompletionResult *result);

#endif // SPLASH_COMPLETE_H
