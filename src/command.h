#ifndef SPLASH_COMMAND_H
#define SPLASH_COMMAND_H

// A single command with its arguments.
typedef struct {
    char **argv;       // NULL-terminated argument array (owned)
    int argc;
    int argv_capacity;
} SimpleCommand;

// A pipeline of one or more commands connected by pipes.
typedef struct {
    SimpleCommand **commands;  // Array of pointers to commands (owned)
    int num_commands;
    int cmd_capacity;
    int background;            // Non-zero if terminated with &
} Pipeline;

// Creates a new empty SimpleCommand. Caller takes ownership.
SimpleCommand *simple_command_new(void);

// Append an argument to the command. The string is copied.
void simple_command_add_arg(SimpleCommand *cmd, const char *arg);

// Free a SimpleCommand and all its arguments.
void simple_command_free(SimpleCommand *cmd);

// Creates a new empty Pipeline. Caller takes ownership.
Pipeline *pipeline_new(void);

// Append a command to the pipeline. Ownership of cmd is transferred to pipeline.
void pipeline_add_command(Pipeline *pl, SimpleCommand *cmd);

// Free a Pipeline and all its commands.
void pipeline_free(Pipeline *pl);

#endif // SPLASH_COMMAND_H
