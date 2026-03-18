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

// Operator connecting two pipelines in a command list.
typedef enum {
    LIST_SEMI,  // ;  — sequential, ignore exit code
    LIST_AND,   // && — execute next only if previous succeeded
    LIST_OR     // || — execute next only if previous failed
} ListOpType;

// A list of one or more pipelines connected by ;, &&, or ||.
typedef struct {
    Pipeline **pipelines;     // Array of pipeline pointers (owned)
    ListOpType *operators;    // Array of operators between pipelines (count = num_pipelines - 1)
    int num_pipelines;
    int pipeline_capacity;
} CommandList;

// Creates a new empty CommandList. Caller takes ownership.
CommandList *command_list_new(void);

// Append a pipeline to the list. Ownership of pl is transferred to list.
void command_list_add_pipeline(CommandList *list, Pipeline *pl);

// Record the operator between the last two pipelines added.
// Must be called after adding the second pipeline.
void command_list_add_operator(CommandList *list, ListOpType op);

// Free a CommandList and all its pipelines.
void command_list_free(CommandList *list);

#endif // SPLASH_COMMAND_H
