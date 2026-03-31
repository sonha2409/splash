#ifndef SPLASH_COMMAND_H
#define SPLASH_COMMAND_H

// Forward declarations for mutual recursion (compound commands contain
// CommandList, CommandList contains Node, Node contains compound commands).
typedef struct CommandList CommandList;
typedef struct IfCommand IfCommand;
typedef struct ForCommand ForCommand;
typedef struct WhileCommand WhileCommand;
typedef struct CaseCommand CaseCommand;
typedef struct FunctionDef FunctionDef;
typedef struct SubshellCommand SubshellCommand;
typedef struct BraceGroupCommand BraceGroupCommand;

typedef enum {
    REDIRECT_OUTPUT,       // >   stdout to file (truncate)
    REDIRECT_APPEND,       // >>  stdout to file (append)
    REDIRECT_INPUT,        // <   stdin from file
    REDIRECT_ERR,          // 2>  stderr to file (truncate)
    REDIRECT_OUT_ERR,      // >&  stdout+stderr to file (truncate)
    REDIRECT_APPEND_ERR,   // >>& stdout+stderr to file (append)
    REDIRECT_HEREDOC,      // <<  stdin from inline text (target = body text)
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

// Operator connecting two entries in a command list.
typedef enum {
    LIST_SEMI,  // ;  — sequential, ignore exit code
    LIST_AND,   // && — execute next only if previous succeeded
    LIST_OR     // || — execute next only if previous failed
} ListOpType;

// Tagged union: an entry in a command list is either a pipeline or a compound command.
typedef enum {
    NODE_PIPELINE,
    NODE_IF,
    NODE_FOR,
    NODE_WHILE,
    NODE_CASE,
    NODE_FUNCTION_DEF,
    NODE_SUBSHELL,
    NODE_BRACE_GROUP,
} NodeType;

typedef struct {
    NodeType type;
    union {
        Pipeline *pipeline;    // NODE_PIPELINE (owned)
        IfCommand *if_cmd;     // NODE_IF (owned)
        ForCommand *for_cmd;   // NODE_FOR (owned)
        WhileCommand *while_cmd; // NODE_WHILE (owned)
        CaseCommand *case_cmd;   // NODE_CASE (owned)
        FunctionDef *func_def;   // NODE_FUNCTION_DEF (owned)
        SubshellCommand *subshell_cmd; // NODE_SUBSHELL (owned)
        BraceGroupCommand *brace_group_cmd; // NODE_BRACE_GROUP (owned)
    };
} Node;

// Free the contents of a Node (dispatches to pipeline_free or if_command_free).
void node_free(Node *node);

// A list of one or more nodes (pipelines/compound commands) connected by ;, &&, or ||.
struct CommandList {
    Node *entries;            // Array of Node structs (owned)
    ListOpType *operators;    // Array of operators between entries (count = num_entries - 1)
    int num_entries;
    int capacity;
};

// Creates a new empty CommandList. Caller takes ownership.
CommandList *command_list_new(void);

// Append a pipeline to the list. Ownership of pl is transferred to list.
void command_list_add_pipeline(CommandList *list, Pipeline *pl);

// Append an if-command to the list. Ownership of if_cmd is transferred to list.
void command_list_add_if(CommandList *list, IfCommand *if_cmd);

// Append a for-command to the list. Ownership of for_cmd is transferred to list.
void command_list_add_for(CommandList *list, ForCommand *for_cmd);

// Append a while-command to the list. Ownership of while_cmd is transferred to list.
void command_list_add_while(CommandList *list, WhileCommand *while_cmd);

// Append a case-command to the list. Ownership of case_cmd is transferred to list.
void command_list_add_case(CommandList *list, CaseCommand *case_cmd);

// Record the operator between the last two entries added.
// Must be called after adding the second entry.
void command_list_add_operator(CommandList *list, ListOpType op);

// Free a CommandList and all its entries.
void command_list_free(CommandList *list);

// One branch of an if/elif chain.
typedef struct {
    CommandList *condition;   // Command list evaluated for exit code (owned)
    CommandList *body;        // Executed if condition succeeds (owned)
} IfClause;

// if condition; then body; [elif condition; then body;]* [else body;] fi
struct IfCommand {
    IfClause *clauses;        // Array: clauses[0]=if, clauses[1..n-1]=elif (owned)
    int num_clauses;
    int clause_capacity;
    CommandList *else_body;   // NULL if no else block (owned)
};

// for var in word...; do commands; done
struct ForCommand {
    char *var_name;           // Loop variable name (owned)
    char **words;             // Word list to iterate over (owned strings)
    int num_words;
    int word_capacity;
    char *body_src;           // Raw source text of body (re-tokenized each iteration, owned)
};

// Creates a new empty ForCommand. Caller takes ownership.
ForCommand *for_command_new(const char *var_name);

// Append a word to the for-command's word list. The string is copied.
void for_command_add_word(ForCommand *cmd, const char *word);

// Free a ForCommand and all its data.
void for_command_free(ForCommand *cmd);

// while/until condition; do commands; done
// is_until: false = while (loop while condition succeeds),
//           true  = until (loop until condition succeeds).
struct WhileCommand {
    char *cond_src;    // Raw condition source text (re-evaluated each iteration, owned)
    char *body_src;    // Raw body source text (re-evaluated each iteration, owned)
    int is_until;      // 0 = while, 1 = until
};

// One clause in a case statement: pattern list + body.
typedef struct {
    char **patterns;       // Array of pattern strings (owned)
    int num_patterns;
    int pattern_capacity;
    char *body_src;        // Raw body source text (re-evaluated, owned)
} CaseClause;

// case word in pattern1) body ;; pattern2) body ;; esac
struct CaseCommand {
    char *word;            // The word to match against (owned)
    CaseClause *clauses;   // Array of clauses (owned)
    int num_clauses;
    int clause_capacity;
};

// Creates a new CaseCommand. Caller takes ownership.
CaseCommand *case_command_new(const char *word);

// Add a pattern to the current (last) clause.
void case_clause_add_pattern(CaseClause *clause, const char *pattern);

// Start a new clause in the case command. Returns a pointer to the new clause.
CaseClause *case_command_add_clause(CaseCommand *cmd);

// Free a CaseCommand and all its data.
void case_command_free(CaseCommand *cmd);

// ( commands ) — subshell: run commands in a forked child process.
struct SubshellCommand {
    char *body_src;    // Raw body source text (owned)
};

// Creates a new SubshellCommand. Caller takes ownership.
SubshellCommand *subshell_command_new(void);

// Free a SubshellCommand and all its data.
void subshell_command_free(SubshellCommand *cmd);

// Append a subshell command to the command list. Ownership transferred.
void command_list_add_subshell(CommandList *list, SubshellCommand *cmd);

// { commands; } — brace group: run commands in the current shell.
struct BraceGroupCommand {
    char *body_src;    // Raw body source text (owned)
};

// Creates a new BraceGroupCommand. Caller takes ownership.
BraceGroupCommand *brace_group_command_new(void);

// Free a BraceGroupCommand and all its data.
void brace_group_command_free(BraceGroupCommand *cmd);

// Append a brace group command to the command list. Ownership transferred.
void command_list_add_brace_group(CommandList *list, BraceGroupCommand *cmd);

// fname() { body; } — shell function definition.
struct FunctionDef {
    char *name;        // Function name (owned)
    char *body_src;    // Raw body source text (owned)
};

// Creates a new FunctionDef. Caller takes ownership.
FunctionDef *function_def_new(const char *name);

// Free a FunctionDef and all its data.
void function_def_free(FunctionDef *def);

// Append a function def to the command list. Ownership transferred.
void command_list_add_function_def(CommandList *list, FunctionDef *def);

// Creates a new WhileCommand. Caller takes ownership.
WhileCommand *while_command_new(int is_until);

// Free a WhileCommand and all its data.
void while_command_free(WhileCommand *cmd);

// Creates a new empty IfCommand. Caller takes ownership.
IfCommand *if_command_new(void);

// Append a clause (condition + body). Ownership transferred.
void if_command_add_clause(IfCommand *cmd, CommandList *condition, CommandList *body);

// Free an IfCommand and all its clauses.
void if_command_free(IfCommand *cmd);

#endif // SPLASH_COMMAND_H
