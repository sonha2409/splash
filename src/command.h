#ifndef SPLASH_COMMAND_H
#define SPLASH_COMMAND_H

typedef enum {
    REDIRECT_OUTPUT,       // >   stdout to file (truncate)
    REDIRECT_APPEND,       // >>  stdout to file (append)
    REDIRECT_INPUT,        // <   stdin from file
    REDIRECT_ERR,          // 2>  stderr to file (truncate)
    REDIRECT_OUT_ERR,      // >&  stdout+stderr to file (truncate)
    REDIRECT_APPEND_ERR,   // >>& stdout+stderr to file (append)
} RedirectType;

typedef struct {
    RedirectType type;
    char *target;          // Filename (owned)
} Redirection;

// A single command with its arguments and redirections.
typedef struct {
    char **argv;           // NULL-terminated argument array (owned)
    int argc;
    int argv_capacity;
    Redirection *redirects; // Dynamic array of redirections (owned)
    int num_redirects;
    int redirect_capacity;
} SimpleCommand;

// Type of pipe connecting two pipeline stages.
typedef enum {
    PIPE_TEXT,        // |  — traditional text pipe
    PIPE_STRUCTURED   // |> — structured data pipe
} PipeType;

// A pipeline of one or more commands connected by pipes.
typedef struct {
    SimpleCommand **commands;  // Array of pointers to commands (owned)
    int num_commands;
    int cmd_capacity;
    int background;            // Non-zero if terminated with &
    PipeType *pipe_types;      // Array of (num_commands - 1) pipe types (owned)
    int pipe_capacity;
} Pipeline;

// Creates a new empty SimpleCommand. Caller takes ownership.
SimpleCommand *simple_command_new(void);

// Append an argument to the command. The string is copied.
void simple_command_add_arg(SimpleCommand *cmd, const char *arg);

// Append a redirection to the command. The target string is copied.
void simple_command_add_redirect(SimpleCommand *cmd, RedirectType type,
                                 const char *target);

// Free a SimpleCommand and all its arguments.
void simple_command_free(SimpleCommand *cmd);

// Creates a new empty Pipeline. Caller takes ownership.
Pipeline *pipeline_new(void);

// Append a command to the pipeline. Ownership of cmd is transferred to pipeline.
void pipeline_add_command(Pipeline *pl, SimpleCommand *cmd);

// Record the pipe type between the last two commands added.
// Must be called after pipeline_add_command for the second command.
void pipeline_add_pipe_type(Pipeline *pl, PipeType type);

// Free a Pipeline and all its commands.
void pipeline_free(Pipeline *pl);

#endif // SPLASH_COMMAND_H
