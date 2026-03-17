#ifndef SPLASH_EXECUTOR_H
#define SPLASH_EXECUTOR_H

#include "command.h"

// Execute a pipeline. Returns the exit status of the last command,
// or -1 on internal error (fork/pipe failure).
int executor_execute(Pipeline *pl);

#endif // SPLASH_EXECUTOR_H
