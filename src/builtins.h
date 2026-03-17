#ifndef SPLASH_BUILTINS_H
#define SPLASH_BUILTINS_H

#include "command.h"
#include "pipeline.h"

// Check if a command is a builtin. Returns 1 if it is, 0 otherwise.
int builtin_is_builtin(const char *name);

// Execute a builtin command. Returns exit status.
// Must only be called after builtin_is_builtin() returns 1.
int builtin_execute(SimpleCommand *cmd);

// Check if a command is a structured builtin (produces PipelineStage for |>).
// Returns 1 if the command produces structured output, 0 otherwise.
int builtin_is_structured(const char *name);

// Create a PipelineStage for a structured builtin command.
// Returns NULL if the command is not a structured builtin.
// upstream may be NULL for source stages; ownership is transferred.
// Caller takes ownership of the returned stage.
PipelineStage *builtin_create_stage(SimpleCommand *cmd,
                                    PipelineStage *upstream);

#endif // SPLASH_BUILTINS_H
