#ifndef SPLASH_EXECUTOR_H
#define SPLASH_EXECUTOR_H

#include "command.h"

// Execute a pipeline. Returns the exit status of the last command,
// or -1 on internal error (fork/pipe failure).
// command_str is the original input line (used for job display).
int executor_execute(Pipeline *pl, const char *command_str);

#endif // SPLASH_EXECUTOR_H
