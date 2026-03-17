#ifndef SPLASH_BUILTINS_H
#define SPLASH_BUILTINS_H

#include "command.h"

// Check if a command is a builtin. Returns 1 if it is, 0 otherwise.
int builtin_is_builtin(const char *name);

// Execute a builtin command. Returns exit status.
// Must only be called after builtin_is_builtin() returns 1.
int builtin_execute(SimpleCommand *cmd);

#endif // SPLASH_BUILTINS_H
