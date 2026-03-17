#ifndef SPLASH_EXECUTOR_H
#define SPLASH_EXECUTOR_H

#include "command.h"

// Execute a pipeline. Returns the exit status of the last command,
// or -1 on internal error (fork/pipe failure).
// command_str is the original input line (used for job display).
int executor_execute(Pipeline *pl, const char *command_str);

// Tokenize, parse, and execute a single line of input.
// Returns the exit status of the executed command, or 0 for empty/parse-error lines.
int executor_execute_line(const char *line);

#endif // SPLASH_EXECUTOR_H
